#!/usr/bin/env python3
"""
MemLLVM flat binary extractor with self-rebase stub.

Turns a linked PE produced by the MemLLVM pipeline into a contiguous,
position-independent blob that can be dropped into arbitrary memory and
executed with a plain `jmp` to its first byte.

This script has no third-party dependency (no pefile); it walks the PE headers
by hand so it can be used as a generic post-link step from any build system.

    python extract_flat_bin.py <input.exe> <output.bin>

Layout of the produced blob
---------------------------
    0x0000                  : `E9 rel32` -> self-rebase stub (the original 5
                              bytes are saved and restored by the stub)
    [base_rva .. max_rva)   : section contents, in virtual-address order
    align 16                : stub
    stub + len(stub)        : DIR64 relocation table (u32 offsets, flat-relative)

Two things used to be wrong here and both are fixed:

1. `base_rva` was taken from `AddressOfEntryPoint`. That is only valid when the
   entry point happens to sit exactly at the start of the packed image, which is
   true for the bundled examples but false in general -- e.g. mimikatz has
   entry RVA 0x1028B0 while its first packed section starts at 0x1000. The
   resulting negative offset silently wrapped around and produced a blob whose
   real entry sat ~1 MB past where the stub jumped.

2. There was no relocation handling at all, so the output was not relocatable.
   Any absolute pointer baked in by the linker (vtables, string literals in
   .rdata, function pointer tables) still pointed at the original ImageBase.
"""

import struct
import sys

# IMAGE_REL_BASED_DIR64
DIR64 = 10

# Sections that carry no loadable content: the relocation directory is consumed
# to build the flat reloc table, and the symbol/import tables do not exist in a
# /nodefaultlib flat link anyway.
# Sections that carry no loadable content: the relocation directory is consumed
# to build the flat reloc table, and the symbol/import tables do not exist in a
# /nodefaultlib flat link anyway.
SKIP_SECTIONS = ('.reloc', '.edata', '.idata', '.rsrc', '.pdata', '.xdata', '.tls')
SECTION_DIAGNOSTICS = {
    '.tls': '[!] Notice: .tls section skipped. Native Thread Local Storage (__declspec(thread)) requires OS TEB initialization not present in flat memory binaries. Use MemLLVM thread-safe local statics or atomics.',
    '.rsrc': '[!] Notice: .rsrc section skipped. Windows PE resource directories (icons, manifest) are not embedded in flat shellcode; embed assets as static byte arrays or load dynamically.',
    '.pdata': '[!] Notice: .pdata section skipped. X64 SEH function table requires OS loader registration via RtlAddFunctionTable; C++ exceptions are disabled (-fno-exceptions).'
}


def _u16(data, off):
    return struct.unpack_from('<H', data, off)[0]


def _u32(data, off):
    return struct.unpack_from('<I', data, off)[0]


def _u64(data, off):
    return struct.unpack_from('<Q', data, off)[0]


def parse_pe(data):
    """Parse the minimal subset of the PE headers this tool needs."""
    if data[:2] != b'MZ':
        raise SystemExit('[-] not a PE file (missing MZ signature)')

    e_lfanew = _u32(data, 0x3C)
    if data[e_lfanew:e_lfanew + 4] != b'PE\0\0':
        raise SystemExit('[-] not a PE file (missing PE signature)')

    coff = e_lfanew + 4
    num_sections = _u16(data, coff + 2)
    opt_size = _u16(data, coff + 16)

    opt = coff + 20
    magic = _u16(data, opt)
    if magic == 0x20B:      # PE32+
        entry_rva = _u32(data, opt + 16)
        image_base = _u64(data, opt + 24)
        num_dirs = _u32(data, opt + 108)
        dir_off = opt + 112
    elif magic == 0x10B:    # PE32
        entry_rva = _u32(data, opt + 16)
        image_base = _u32(data, opt + 28)
        num_dirs = _u32(data, opt + 92)
        dir_off = opt + 96
    else:
        raise SystemExit('[-] unsupported optional header magic 0x%X' % magic)

    sections = []
    sec_off = opt + opt_size
    for i in range(num_sections):
        hdr = sec_off + i * 40
        name = data[hdr:hdr + 8].rstrip(b'\0').decode('latin1', errors='ignore')
        virt_size, virt_addr, raw_size, raw_ptr = struct.unpack_from('<IIII', data, hdr + 8)
        # In a flat in-memory image, a section's span is its virtual memory footprint (virt_size),
        # not the raw PE disk alignment padding (raw_size).
        span = virt_size if virt_size > 0 else raw_size
        sections.append({
            'name': name,
            'virt_size': virt_size,
            'virt_addr': virt_addr,
            'raw_size': raw_size,
            'raw_ptr': raw_ptr,
            'span': span,
        })

    # DataDirectory[5] == IMAGE_DIRECTORY_ENTRY_BASERELOC
    reloc_rva = reloc_size = 0
    if num_dirs > 5:
        reloc_rva = _u32(data, dir_off + 5 * 8)
        reloc_size = _u32(data, dir_off + 5 * 8 + 4)

    return {
        'entry_rva': entry_rva,
        'image_base': image_base,
        'sections': sections,
        'reloc_rva': reloc_rva,
        'reloc_size': reloc_size,
    }


def rva_to_file_off(sections, rva):
    for sec in sections:
        if sec['virt_addr'] <= rva < sec['virt_addr'] + sec['span']:
            delta = rva - sec['virt_addr']
            if delta >= sec['raw_size']:
                return None   # inside the zero-filled tail of the section
            return sec['raw_ptr'] + delta
    return None


def collect_dir64_relocs(data, pe, base_rva, total_size):
    """Return flat-relative offsets of every IMAGE_REL_BASED_DIR64 fixup."""
    relocs = []
    if not pe['reloc_rva'] or not pe['reloc_size']:
        return relocs

    off = rva_to_file_off(pe['sections'], pe['reloc_rva'])
    if off is None:
        return relocs

    end = off + pe['reloc_size']
    while off + 8 <= end:
        page_rva = _u32(data, off)
        block_size = _u32(data, off + 4)
        if block_size < 8:
            break
        entry_off = off + 8
        block_end = min(off + block_size, end)
        while entry_off + 2 <= block_end:
            word = _u16(data, entry_off)
            etype = word >> 12
            if etype == DIR64:
                target = page_rva + (word & 0xFFF) - base_rva
                if 0 <= target < total_size:
                    relocs.append(target)
            elif etype != 0:
                print('[!] skipping non-DIR64 relocation type %d at RVA 0x%X'
                      % (etype, page_rva + (word & 0xFFF)))
            entry_off += 2
        off += block_size

    return relocs


def build_stub(stub_offset, orig_base, entry_offset, num_relocs,
               reloc_table_offset, orig5):
    """Emit the x64 self-rebase stub. Identical encoding to build_chip8.py."""
    code = bytearray()

    # 0. Preserve caller argument registers (rcx, rdx, r8, r9).
    code.extend(b'\x51')                              # push rcx
    code.extend(b'\x52')                              # push rdx
    code.extend(b'\x41\x50')                          # push r8
    code.extend(b'\x41\x51')                          # push r9

    # 1. Get RIP into RDX, then turn it into the real load address of the blob.
    code.extend(b'\xE8\x00\x00\x00\x00\x5A')          # call $+5 ; pop rdx
    pop_rdx_off = len(code) - 1
    code.extend(b'\x48\x81\xEA' + struct.pack('<I', stub_offset + pop_rdx_off))

    # 2. r8 = Delta = current_base - orig_base
    code.extend(b'\x48\xB8' + struct.pack('<Q', orig_base))   # mov rax, orig_base
    code.extend(b'\x49\x89\xD0')                              # mov r8, rdx
    code.extend(b'\x49\x29\xC0')                              # sub r8, rax

    # 3. jz skip_rebase  (nothing to do when the blob landed at its link address)
    jz_idx = len(code)
    code.extend(b'\x74\x00')

    # 4. Walk the DIR64 table: for each offset, [rdx + off] += Delta
    if num_relocs > 0:
        code.extend(b'\x4C\x8D\x8A' + struct.pack('<I', reloc_table_offset))
        code.extend(b'\x41\xBA' + struct.pack('<I', num_relocs))

        loop_start = len(code)
        code.extend(b'\x41\x8B\x01')        # mov eax, dword ptr [r9]
        code.extend(b'\x4C\x01\x04\x02')    # add qword ptr [rdx + rax], r8
        code.extend(b'\x49\x83\xC1\x04')    # add r9, 4
        code.extend(b'\x41\xFF\xCA')        # dec r10d
        loop_disp = loop_start - (len(code) + 2)
        code.extend(b'\x75' + struct.pack('b', loop_disp))

    skip_target = len(code)
    code[jz_idx + 1] = skip_target - (jz_idx + 2)

    # 5. Restore the 5 bytes we clobbered with the entry jmp.
    orig_dw, orig_b = struct.unpack('<IB', orig5)
    code.extend(b'\xC7\x02' + struct.pack('<I', orig_dw))
    code.extend(b'\xC6\x42\x04' + struct.pack('<B', orig_b))

    # 6. Compute entry point in rax, restore caller argument registers, and jmp.
    code.extend(b'\x48\x8D\x82' + struct.pack('<I', entry_offset))  # lea rax,[rdx+entry]
    code.extend(b'\x41\x59')                          # pop r9
    code.extend(b'\x41\x58')                          # pop r8
    code.extend(b'\x5A')                              # pop rdx
    code.extend(b'\x59')                              # pop rcx
    code.extend(b'\xFF\xE0')                          # jmp rax

    return bytes(code)


def make_flat_bin(exe_path, out_bin_path):
    with open(exe_path, 'rb') as f:
        data = f.read()

    pe = parse_pe(data)
    for s in pe['sections']:
        if s['name'] in SECTION_DIAGNOSTICS:
            print(SECTION_DIAGNOSTICS[s['name']])
    packed = [s for s in pe['sections']
              if s['name'] not in SKIP_SECTIONS and s['span'] > 0]
    if not packed:
        raise SystemExit('[-] no loadable sections found in %s' % exe_path)

    # base_rva is the lowest virtual address among the packed sections -- NOT
    # the entry point. See the module docstring for why this matters.
    base_rva = min(s['virt_addr'] for s in packed)
    max_rva = max(s['virt_addr'] + s['span'] for s in packed)
    orig_base = pe['image_base'] + base_rva

    entry_offset = pe['entry_rva'] - base_rva
    if entry_offset < 0:
        raise SystemExit(
            '[-] entry point RVA 0x%X is below the packed image base 0x%X; '
            'the entry section is not part of the flat image'
            % (pe['entry_rva'], base_rva))

    print('[*] Processing PE: %s' % exe_path)
    print('  ImageBase      : 0x%X' % pe['image_base'])
    print('  Base RVA       : 0x%X  (orig_base = 0x%X)' % (base_rva, orig_base))
    print('  Entry RVA      : 0x%X  (flat offset = 0x%X)'
          % (pe['entry_rva'], entry_offset))

    total_size = max_rva - base_rva
    flat_buf = bytearray(total_size)

    for sec in packed:
        off = sec['virt_addr'] - base_rva
        actual_raw = min(sec['raw_size'], sec['span']) if sec['raw_size'] else 0
        raw = data[sec['raw_ptr']:sec['raw_ptr'] + actual_raw] if actual_raw else b''
        if raw:
            flat_buf[off:off + len(raw)] = raw
        if sec['span'] > len(raw):
            flat_buf[off + len(raw):off + sec['span']] = b'\0' * (sec['span'] - len(raw))
        print('  Section %-8s RVA=0x%-6X span=0x%-6X raw=0x%-6X -> off 0x%X'
              % (sec['name'], sec['virt_addr'], sec['span'], sec['raw_size'], off))

    print('[+] Flat image size: %d bytes (0x%X)' % (total_size, total_size))

    relocs = collect_dir64_relocs(data, pe, base_rva, total_size)
    print('[+] DIR64 relocations: %d' % len(relocs))

    # Displace the first 5 bytes with a jmp into the stub; the stub puts them back.
    if total_size < 5:
        raise SystemExit('[-] flat image too small to host the entry stub')
    orig5 = bytes(flat_buf[:5])

    stub_offset = (len(flat_buf) + 15) & ~15
    flat_buf.extend(b'\0' * (stub_offset - len(flat_buf)))

    # Two-pass: the stub length depends on reloc_table_offset, which depends on
    # the stub length. The length is invariant to the value, so measure first.
    dummy = build_stub(stub_offset, orig_base, entry_offset, len(relocs), 0, orig5)
    reloc_table_offset = stub_offset + len(dummy)
    stub = build_stub(stub_offset, orig_base, entry_offset, len(relocs),
                      reloc_table_offset, orig5)
    assert len(stub) == len(dummy)

    reloc_bytes = struct.pack('<%dI' % len(relocs), *relocs) if relocs else b''

    flat_buf[:5] = struct.pack('<BI', 0xE9, stub_offset - 5)
    flat_buf.extend(stub)
    flat_buf.extend(reloc_bytes)

    with open(out_bin_path, 'wb') as f:
        f.write(flat_buf)

    print('[+] Stub at 0x%X, reloc table at 0x%X' % (stub_offset, reloc_table_offset))
    print('[+] Wrote %s (%d bytes, %.2f KB)'
          % (out_bin_path, len(flat_buf), len(flat_buf) / 1024.0))


def main(argv):
    if len(argv) != 3:
        print('usage: python extract_flat_bin.py <input.exe> <output.bin>')
        return 2
    in_path, out_path = argv[1], argv[2]
    if in_path.endswith('.bin') and out_path.endswith('.exe'):
        in_path, out_path = out_path, in_path
    make_flat_bin(in_path, out_path)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
