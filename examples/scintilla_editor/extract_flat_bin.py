import struct
import sys

def make_flat_bin(exe_path, out_bin_path):
    with open(exe_path, 'rb') as f:
        data = f.read()

    e_lfanew = struct.unpack_from('<I', data, 0x3C)[0]
    magic = data[e_lfanew:e_lfanew+4]
    assert magic == b'PE\0\0'

    num_sections = struct.unpack_from('<H', data, e_lfanew + 4 + 2)[0]
    size_of_opt_hdr = struct.unpack_from('<H', data, e_lfanew + 4 + 16)[0]

    opt_hdr_offset = e_lfanew + 4 + 20
    opt_magic = struct.unpack_from('<H', data, opt_hdr_offset)[0]
    assert opt_magic == 0x20B

    entry_point = struct.unpack_from('<I', data, opt_hdr_offset + 16)[0]
    section_headers_offset = opt_hdr_offset + size_of_opt_hdr

    sections = []
    for i in range(num_sections):
        sec_offset = section_headers_offset + i * 40
        name = data[sec_offset:sec_offset+8].rstrip(b'\0').decode('latin1', errors='ignore')
        virt_size, virt_addr, raw_size, raw_ptr = struct.unpack_from('<IIII', data, sec_offset + 8)
        sections.append({
            'name': name,
            'virt_size': virt_size,
            'virt_addr': virt_addr,
            'raw_size': raw_size,
            'raw_ptr': raw_ptr
        })

    base_rva = entry_point
    print(f"[+] Entry Point RVA: 0x{entry_point:X}")

    max_rva = 0
    for sec in sections:
        if sec['name'] in ('.text', '.rdata', '.data', '.bss', '.text_rd', '.text_da', '.text_bs'):
            end_rva = sec['virt_addr'] + max(sec['virt_size'], sec['raw_size'])
            if end_rva > max_rva:
                max_rva = end_rva
            print(f"  Section {sec['name']}: RVA=0x{sec['virt_addr']:X}, Size=0x{sec['virt_size']:X}, RawSize=0x{sec['raw_size']:X}")

    total_size = max_rva - base_rva
    print(f"[+] Flat Binary Total Memory Size: {total_size} bytes (0x{total_size:X})")
    flat_buf = bytearray(total_size)

    for sec in sections:
        if sec['name'] in ('.text', '.rdata', '.data', '.text_rd', '.text_da'):
            rva_offset = sec['virt_addr'] - base_rva
            sec_raw = data[sec['raw_ptr'] : sec['raw_ptr'] + sec['raw_size']]
            flat_buf[rva_offset : rva_offset + len(sec_raw)] = sec_raw

    with open(out_bin_path, 'wb') as f:
        f.write(flat_buf)
    print(f"[+] Successfully wrote flat machine code to: {out_bin_path}")

if __name__ == '__main__':
    make_flat_bin(sys.argv[1], sys.argv[2])
