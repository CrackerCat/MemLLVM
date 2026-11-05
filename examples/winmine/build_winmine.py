import os
import sys
import subprocess
import shutil

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))

def resolve_llvm_tool(tool_name):
    for var in ["LLVM_PATH", "LLVM_DIR", "LLVM_ROOT"]:
        base = os.environ.get(var)
        if base and os.path.exists(base):
            cand = os.path.join(base, "bin", f"{tool_name}.exe")
            if os.path.exists(cand):
                return cand
            cand = os.path.join(base, f"{tool_name}.exe")
            if os.path.exists(cand):
                return cand
    for cand in [
        rf"C:\LLVM-22-plugin\bin\{tool_name}.exe",
        rf"C:\LLVM-22-dev\bin\{tool_name}.exe",
        rf"C:\Program Files\LLVM\bin\{tool_name}.exe",
    ]:
        if os.path.exists(cand):
            return cand
    w = shutil.which(tool_name) or shutil.which(f"{tool_name}.exe")
    if w:
        return w
    if tool_name == "lld-link":
        link = shutil.which("link") or shutil.which("link.exe")
        if link:
            return link
    print(f"[-] ERROR: Required tool '{tool_name}' not found!", file=sys.stderr)
    sys.exit(1)

def resolve_rc_and_sdk_includes():
    base_bin = r"C:\Program Files (x86)\Windows Kits\10\bin"
    base_inc = r"C:\Program Files (x86)\Windows Kits\10\Include"
    rc_exe = shutil.which("rc.exe") or shutil.which("rc")
    um_inc = None
    shared_inc = None

    if os.path.exists(base_inc):
        versions = [d for d in os.listdir(base_inc) if d.startswith("10.") and os.path.isdir(os.path.join(base_inc, d))]
        def ver_key(v):
            try:
                return [int(x) for x in v.split(".")]
            except Exception:
                return [0]
        versions.sort(key=ver_key, reverse=True)
        for v in versions:
            u = os.path.join(base_inc, v, "um")
            s = os.path.join(base_inc, v, "shared")
            if os.path.isdir(u) and os.path.isdir(s):
                if not um_inc:
                    um_inc = u
                    shared_inc = s
                if not rc_exe:
                    cand_rc = os.path.join(base_bin, v, "x64", "rc.exe")
                    if os.path.exists(cand_rc):
                        rc_exe = cand_rc
                if rc_exe and um_inc:
                    break

    if not rc_exe:
        for cand in [
            r"C:\LLVM-22-plugin\bin\llvm-rc.exe",
            r"C:\LLVM-22-dev\bin\llvm-rc.exe",
        ]:
            if os.path.exists(cand):
                rc_exe = cand
                break
        if not rc_exe:
            rc_exe = shutil.which("llvm-rc.exe") or shutil.which("llvm-rc")

    if not rc_exe:
        print("[-] ERROR: Resource compiler (rc.exe / llvm-rc.exe) not found!", file=sys.stderr)
        sys.exit(1)

    return rc_exe, um_inc, shared_inc

CLANG_CL = resolve_llvm_tool("clang-cl")
LLD_LINK = resolve_llvm_tool("lld-link")
RC_EXE, UM_INC, SHARED_INC = resolve_rc_and_sdk_includes()

PLUGIN_DLL = os.environ.get("PLUGIN_DLL")
if not PLUGIN_DLL or not os.path.exists(PLUGIN_DLL):
    for cand in [
        os.path.join(REPO_ROOT, "bin", "MemLLVM.dll"),
        os.path.join(REPO_ROOT, "build", "MemLLVM", "Release", "MemLLVM.dll"),
        os.path.join(REPO_ROOT, "build", "Release", "MemLLVM.dll"),
        os.path.join(REPO_ROOT, "release_artifacts", "MemLLVM.dll")
    ]:
        if os.path.exists(cand):
            PLUGIN_DLL = cand
            break

if not PLUGIN_DLL or not os.path.exists(PLUGIN_DLL):
    print("[-] ERROR: MemLLVM.dll plugin not found!", file=sys.stderr)
    sys.exit(1)

LTL_LIB = os.path.join(REPO_ROOT, "ltl", "ltl.lib")
LTL_INC = os.path.join(REPO_ROOT, "ltl", "include")
EXTRACT_BIN = os.path.join(REPO_ROOT, "Tools", "extract_flat_bin.py")

WINMINE_RC = os.path.join(SCRIPT_DIR, "winmine.rc")
WINMINE_RES = os.path.join(SCRIPT_DIR, "winmine.res")
MAIN_C = os.path.join(SCRIPT_DIR, "main.c")
DIALOG_C = os.path.join(SCRIPT_DIR, "dialog.c")
MAIN_OBJ = os.path.join(SCRIPT_DIR, "main.obj")
DIALOG_OBJ = os.path.join(SCRIPT_DIR, "dialog.obj")
WINMINE_EXE = os.path.join(SCRIPT_DIR, "winmine.exe")
WINMINE_BIN = os.path.join(SCRIPT_DIR, "winmine.bin")

def main():
    print("=======================================================================")
    print("    MemLLVM Showcase: ReactOS WineMine (Classic Win32 Minesweeper)     ")
    print("=======================================================================\n")

    print(f"[*] Compiler   : {CLANG_CL}")
    print(f"[*] Linker     : {LLD_LINK}")
    print(f"[*] RC         : {RC_EXE}")
    print(f"[*] SDK UM Inc : {UM_INC}")
    print(f"[*] Plugin     : {PLUGIN_DLL}")

    # 1. Compile resource script
    print("\n[*] [Stage 1/4] Compiling Windows resource script (winmine.rc)...")
    rc_cmd = [RC_EXE]
    if UM_INC:
        rc_cmd.extend(["/i", UM_INC])
    if SHARED_INC:
        rc_cmd.extend(["/i", SHARED_INC])
    rc_cmd.extend(["/fo", WINMINE_RES, WINMINE_RC])

    res = subprocess.run(rc_cmd, cwd=SCRIPT_DIR)
    if res.returncode != 0:
        print("[-] RC compilation failed!")
        return 1

    # 2. Compile C sources
    print("\n[*] [Stage 2/4] Compiling C sources with MemLLVM plugin...")
    common_cflags = [
        CLANG_CL, "/c", "/nologo", "/W3",
        "--target=x86_64-pc-windows-msvc",
        "-m64", "-D_WIN64", "-D_AMD64_",
        f"/clang:-fpass-plugin={PLUGIN_DLL}",
        "/GS-", "/clang:-ffreestanding", "/clang:-fno-builtin",
        "/clang:-fno-jump-tables", "/clang:-fno-exceptions", "/clang:-fno-rtti",
        "/D_HAS_EXCEPTIONS=0",
        "/Gy", "/clang:-ffunction-sections", "/clang:-fdata-sections",
        f"-I{LTL_INC}",
        f"-I{SCRIPT_DIR}",
        "/O1", "/clang:-Oz",
        "/DUNICODE", "/D_UNICODE"
    ]

    for src, obj in [(MAIN_C, MAIN_OBJ), (DIALOG_C, DIALOG_OBJ)]:
        print(f"    Compiling {os.path.basename(src)} -> {os.path.basename(obj)}...")
        res = subprocess.run(common_cflags + [src, f"/Fo{obj}"])
        if res.returncode != 0:
            print(f"[-] Compilation of {src} failed!")
            return 1

    # 3. Link executable
    print("\n[*] [Stage 3/4] Linking winmine.exe with ltl.lib & winmine.res...")
    link_cmd = [
        LLD_LINK, "/nologo", "/nodefaultlib",
        "/entry:main", "/subsystem:windows",
        "/opt:ref", "/opt:icf",
        "/align:512",
        "/merge:.rdata=.text",
        f"/out:{WINMINE_EXE}",
        MAIN_OBJ, DIALOG_OBJ, WINMINE_RES,
        LTL_LIB
    ]
    res = subprocess.run(link_cmd)
    if res.returncode != 0:
        print("[-] Linking failed!")
        return 1

    # 4. Extract flat binary
    print("\n[*] [Stage 4/4] Extracting flat position-independent binary (winmine.bin)...")
    extract_cmd = [sys.executable, EXTRACT_BIN, WINMINE_EXE, WINMINE_BIN]
    res = subprocess.run(extract_cmd)
    if res.returncode != 0:
        print("[-] Extracting flat binary failed!")
        return 1

    # Mirror to release_artifacts
    rel_art_dst = os.path.join(REPO_ROOT, "release_artifacts", "winmine.bin")
    if os.path.exists(os.path.dirname(rel_art_dst)):
        try:
            shutil.copy2(WINMINE_BIN, rel_art_dst)
            print(f"[+] Also copied to {rel_art_dst}")
        except Exception:
            pass

    size_kb = os.path.getsize(WINMINE_BIN) / 1024.0
    print("\n" + "=" * 75)
    print(f"[+] SUCCESS: Standalone Flat Binary Minesweeper generated: {WINMINE_BIN} ({size_kb:.2f} KB)")
    print("=" * 75)
    return 0

if __name__ == "__main__":
    sys.exit(main())
