import os
import sys
import subprocess
import struct
import pefile
import shutil

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))

def resolve_llvm_tool(tool_name):
    env_direct = os.environ.get(tool_name.upper().replace("-", "_"))
    if env_direct:
        if os.path.exists(env_direct):
            return env_direct
        w = shutil.which(env_direct)
        if w:
            return w

    for var in ["LLVM_PATH", "LLVM_DIR", "LLVM_ROOT"]:
        base = os.environ.get(var)
        if base and os.path.exists(base):
            cand = os.path.join(base, "bin", f"{tool_name}.exe")
            if os.path.exists(cand):
                return cand
            cand = os.path.join(base, f"{tool_name}.exe")
            if os.path.exists(cand):
                return cand

    w = shutil.which(tool_name) or shutil.which(f"{tool_name}.exe")
    if w:
        return w

    print("=" * 72, file=sys.stderr)
    print(f"[-] ERROR: Required LLVM tool '{tool_name}' could not be located!", file=sys.stderr)
    print("[-] You MUST configure the LLVM environment variable before building:", file=sys.stderr)
    print("[-]   CMD:        set LLVM_PATH=C:\\path\\to\\llvm", file=sys.stderr)
    print("[-]   PowerShell: $env:LLVM_PATH = \"C:\\path\\to\\llvm\"", file=sys.stderr)
    print("[-]   Permanent:  setx LLVM_PATH \"C:\\path\\to\\llvm\"", file=sys.stderr)
    print("=" * 72, file=sys.stderr)
    sys.exit(1)

CLANG_CL = resolve_llvm_tool("clang-cl")
LLD_LINK = resolve_llvm_tool("lld-link")

PLUGIN_DLL = os.environ.get("PLUGIN_DLL")
if not PLUGIN_DLL or not os.path.exists(PLUGIN_DLL):
    for cand in [
        os.path.join(REPO_ROOT, "build", "MemLLVM", "Release", "MemLLVM.dll"),
        os.path.join(REPO_ROOT, "build", "Release", "MemLLVM.dll"),
        os.path.join(REPO_ROOT, "release_artifacts", "MemLLVM.dll")
    ]:
        if os.path.exists(cand):
            PLUGIN_DLL = cand
            break

if not PLUGIN_DLL or not os.path.exists(PLUGIN_DLL):
    print(f"[-] ERROR: MemLLVM plugin DLL not found! Please build MemLLVM first or set PLUGIN_DLL.", file=sys.stderr)
    sys.exit(1)

LTL_LIB = os.path.join(REPO_ROOT, "ltl", "ltl.lib")
LTL_INC = os.path.join(REPO_ROOT, "ltl", "include")
WORK_DIR = SCRIPT_DIR
OBJ_DIR = os.path.join(WORK_DIR, "obj")
os.makedirs(OBJ_DIR, exist_ok=True)

SOURCES = [
    os.path.join(WORK_DIR, "chip8_core.cpp"),
    os.path.join(WORK_DIR, "main.cpp")
]

DEFINES = [
    "/std:c++20",
    "-DNDEBUG",
    "-D_WINDOWS",
    "-D_WIN64",
    "-DNOMINMAX",
    "/GS-",
    "/GR-",
    "/Gy",
    "/clang:-ffunction-sections",
    "/clang:-fdata-sections",
    "/clang:-fno-exceptions",
    "/clang:-fno-builtin",
    "/O1",
    "/clang:-Oz",
    f"/clang:-fpass-plugin={PLUGIN_DLL}"
]

INCLUDES = [
    f"-I{LTL_INC}",
    f"-I{WORK_DIR}"
]

def compile_sources():
    objs = []
    for src in SOURCES:
        name = os.path.splitext(os.path.basename(src))[0] + ".obj"
        obj_path = os.path.join(OBJ_DIR, name)
        cmd = [CLANG_CL] + DEFINES + INCLUDES + ["-c", src, f"/Fo:{obj_path}"]
        print(f"[*] Compiling {os.path.basename(src)} with MemLLVM...")
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            print(f"[-] Compilation failed for {src}:")
            print(res.stderr)
            print(res.stdout)
            sys.exit(1)
        objs.append(obj_path)
    return objs

def build_stub(stub_offset, orig_base, entry_offset, num_relocs, reloc_table_offset, orig5):
    code = bytearray()
    
    # 0. Preserve caller argument registers (rcx, rdx, r8, r9)
    code.extend(b'\x51')     # push rcx
    code.extend(b'\x52')     # push rdx
    code.extend(b'\x41\x50') # push r8
    code.extend(b'\x41\x51') # push r9

    # 1. call get_rip to get current RIP into RDX
    code.extend(b'\xE8\x00\x00\x00\x00\x5A') # call $+5; pop rdx
    pop_rdx_off = len(code) - 1
    
    # sub rdx, (stub_offset + pop_rdx_off) -> RDX = current_base
    code.extend(b'\x48\x81\xEA' + struct.pack('<I', stub_offset + pop_rdx_off))
    
    # 2. Delta = current_base - orig_base (r8 = Delta)
    code.extend(b'\x48\xB8' + struct.pack('<Q', orig_base)) # mov rax, orig_base
    code.extend(b'\x49\x89\xD0')                             # mov r8, rdx
    code.extend(b'\x49\x29\xC0')                             # sub r8, rax (r8 = Delta)
    
    # 3. jz skip_rebase
    jz_idx = len(code)
    code.extend(b'\x74\x00')
    
    # 4. lea r9, [rdx + reloc_table_offset]; mov r10d, num_relocs
    if num_relocs > 0:
        code.extend(b'\x4C\x8D\x8A' + struct.pack('<I', reloc_table_offset))
        code.extend(b'\x41\xBA' + struct.pack('<I', num_relocs))
        
        # Rebase loop
        loop_start = len(code)
        code.extend(b'\x41\x8B\x01')         # mov eax, dword ptr [r9]
        code.extend(b'\x4C\x01\x04\x02')     # add qword ptr [rdx + rax], r8
        code.extend(b'\x49\x83\xC1\x04')     # add r9, 4
        code.extend(b'\x41\xFF\xCA')         # dec r10d
        loop_disp = loop_start - (len(code) + 2)
        code.extend(b'\x75' + struct.pack('b', loop_disp))
    
    # skip_rebase target
    skip_target = len(code)
    code[jz_idx + 1] = skip_target - (jz_idx + 2)
    
    # 5. Restore original 5 bytes at offset 0
    orig_dw, orig_b = struct.unpack('<IB', orig5)
    code.extend(b'\xC7\x02' + struct.pack('<I', orig_dw))     # mov dword ptr [rdx], orig_dw
    code.extend(b'\xC6\x42\x04' + struct.pack('<B', orig_b))   # mov byte ptr [rdx + 4], orig_b
    
    # 6. Compute entry point in rax, restore caller argument registers, and jmp
    code.extend(b'\x48\x8D\x82' + struct.pack('<I', entry_offset)) # lea rax, [rdx + entry_offset]
    code.extend(b'\x41\x59')                                       # pop r9
    code.extend(b'\x41\x58')                                       # pop r8
    code.extend(b'\x5A')                                           # pop rdx
    code.extend(b'\x59')                                           # pop rcx
    code.extend(b'\xFF\xE0')                                       # jmp rax
    
    return bytes(code)

def extract_flat_bin_with_selfrebase(exe_path, out_bin_path):
    print(f"[*] Processing PE: {exe_path}")
    pe = pefile.PE(exe_path)
    
    LOADABLE_SECTIONS = ('.text', '.rdata', '.data', '.bss')
    loadable = [s for s in pe.sections if s.Name.decode().rstrip('\0') in LOADABLE_SECTIONS]
    if not loadable:
        raise ValueError("No loadable sections found in PE!")
    base_rva = min(s.VirtualAddress for s in loadable)
    image_base = pe.OPTIONAL_HEADER.ImageBase
    orig_base = image_base + base_rva
    entry_point_rva = pe.OPTIONAL_HEADER.AddressOfEntryPoint
    entry_offset = entry_point_rva - base_rva

    print(f"  ImageBase: 0x{image_base:X}")
    print(f"  Base RVA: 0x{base_rva:X}")
    print(f"  EntryPoint RVA: 0x{entry_point_rva:X} (Offset in flat binary: 0x{entry_offset:X})")

    with open(exe_path, 'rb') as f:
        exe_data = f.read()

    max_rva = 0
    for s in pe.sections:
        name = s.Name.decode().rstrip('\0')
        if name in LOADABLE_SECTIONS:
            span = max(s.Misc_VirtualSize, s.SizeOfRawData)
            end = s.VirtualAddress + span
            if end > max_rva:
                max_rva = end
            print(f"  Section {name}: RVA=0x{s.VirtualAddress:X}, Size=0x{s.Misc_VirtualSize:X}, RawSize=0x{s.SizeOfRawData:X}")

    total_size = max_rva - base_rva
    flat_buf = bytearray(total_size)
    for s in pe.sections:
        name = s.Name.decode().rstrip('\0')
        if name in LOADABLE_SECTIONS:
            off = s.VirtualAddress - base_rva
            span = max(s.Misc_VirtualSize, s.SizeOfRawData)
            if s.SizeOfRawData > 0:
                raw = exe_data[s.PointerToRawData : s.PointerToRawData + s.SizeOfRawData]
                flat_buf[off : off + len(raw)] = raw
            # Explicitly zero any uninitialized/BSS tail (VirtualSize > SizeOfRawData)
            if span > s.SizeOfRawData:
                flat_buf[off + s.SizeOfRawData : off + span] = b'\0' * (span - s.SizeOfRawData)

    # Extract DIR64 relocations from .reloc
    relocs = []
    if hasattr(pe, 'DIRECTORY_ENTRY_BASERELOC'):
        for br in pe.DIRECTORY_ENTRY_BASERELOC:
            for entry in br.entries:
                if entry.type == 10: # IMAGE_REL_BASED_DIR64
                    off = entry.rva - base_rva
                    if 0 <= off < total_size:
                        relocs.append(off)

    print(f"[+] Total DIR64 relocations extracted: {len(relocs)}")

    # We displace the first 5 bytes at offset 0x00 with JMP stub
    orig5 = bytes(flat_buf[:5])

    stub_offset = (len(flat_buf) + 15) & ~15
    flat_buf.extend(b'\0' * (stub_offset - len(flat_buf)))

    # Dummy stub to determine length
    dummy_stub = build_stub(stub_offset, orig_base, entry_offset, len(relocs), 0, orig5)
    reloc_table_offset = stub_offset + len(dummy_stub)

    # Actual stub with accurate reloc_table_offset
    stub = build_stub(stub_offset, orig_base, entry_offset, len(relocs), reloc_table_offset, orig5)
    reloc_bytes = struct.pack(f'<{len(relocs)}I', *relocs) if len(relocs) > 0 else b''
    full_payload = stub + reloc_bytes

    # Patch offset 0x00 with JMP to stub
    jmp_to_stub = struct.pack('<BI', 0xE9, stub_offset - 5)
    flat_buf[:5] = jmp_to_stub
    flat_buf.extend(full_payload)

    pe.close()
    with open(out_bin_path, 'wb') as f:
        f.write(flat_buf)

    print(f"[+] Successfully generated self-relocating flat binary: {out_bin_path}")
    print(f"[+] Total size: {len(flat_buf)} bytes ({len(flat_buf)/1024:.2f} KB)")

def main():
    objs = compile_sources()
    temp_exe = os.path.join(WORK_DIR, "chip8_temp.exe")
    out_bin = os.path.join(WORK_DIR, "chip8.bin")

    link_cmd = [
        LLD_LINK,
        "/subsystem:windows",
        "/entry:main",
        "/nodefaultlib",
        "/safeseh:no",
        "/opt:ref",
        "/opt:icf",
        "/align:512",
        "/merge:.rdata=.text",
        f"/out:{temp_exe}",
        LTL_LIB
    ] + objs

    print("[*] Linking with lld-link...")
    res = subprocess.run(link_cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print("[-] Linking failed:")
        print(res.stderr)
        print(res.stdout)
        sys.exit(1)

    extract_flat_bin_with_selfrebase(temp_exe, out_bin)

    # Clean up intermediate exe and pdb/ilk
    if os.path.exists(temp_exe):
        os.remove(temp_exe)
    for ext in (".pdb", ".ilk", ".map"):
        p = os.path.splitext(temp_exe)[0] + ext
        if os.path.exists(p):
            os.remove(p)
    shutil.rmtree(OBJ_DIR, ignore_errors=True)

    print("\n[+] Build complete! Flat binary ready at:")
    print(f"    {out_bin}")

if __name__ == "__main__":
    main()
