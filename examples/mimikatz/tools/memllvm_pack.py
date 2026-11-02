#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
MemLLVM 扁平镜像打包器（自带重定位引导）

为什么需要它
------------
MemLLVM SDK 的 readme 第 10 章第 8 步让你用
    python "$(MEMLLVM_DIR)\\examples\\imgui\\extract_flat_bin.py" ...
但那个脚本是「最简版」，它做的是：

    base_rva = entry_point

即假定「PE 入口点就是扁平镜像的第一个字节」。这只在
`/ENTRY:main` 且 main 恰好排在 .text 最前面时成立。
mimikatz 的入口是 CRT 启动代码（RVA 0x1028B0），而 .text 从 0x1000 开始，
相对偏移是负数；Python 的负索引切片会被钳到 0，于是 .text 从偏移 0 开始覆盖写，
并且长度不匹配的 slice 赋值还会把 bytearray 撑大（脚本自算 650KB，实际写出 1.7MB）。
结果就是 bin 里真正的入口代码落在 0x1018B0，而加载器从偏移 0 跳 → 跑错函数。

readme 第 5 章描述的才是正确做法：提取器要
  1) 收集 PE 的 IMAGE_REL_BASED_DIR64 重定位，生成重定位表；
  2) 在镜像末尾追加一个自重定位引导 stub；
  3) 把镜像开头 5 字节改成 jmp stub；
  4) stub 在运行时算出实际加载基址与编译基址的差值，逐项修补指针，
     再把开头 5 字节还原，最后跳到真正的入口。

SDK 里 examples/{chip8,box2d,doom}/build_*.py 里的 extract_flat_bin_with_selfrebase()
就是这个实现。本脚本把它独立出来、去掉 pefile 依赖，供 vcxproj 后置事件调用。

用法
----
    python memllvm_pack.py <input.exe> <output.bin>
"""

import struct
import sys


# 参与打包的节。代码/只读数据/可写数据，其余（.pdata/.reloc/.rsrc/.idata）丢弃。
# .mli 是 MemLLVM 的跨 TU 初始化表（.mli$A / .mli$M / .mli$Z 由链接器合并而来），
# 入口 TU 的 main 会遍历它逐个调用各 TU 的 init 函数，必须一起打包。
DATA_SECTIONS = ('.text', '.rdata', '.data', '.mli')
BSS_SECTIONS = ('.bss', '.text_bs')
ALL_SECTIONS = DATA_SECTIONS + BSS_SECTIONS + ('.text_rd', '.text_da')

IMAGE_REL_BASED_DIR64 = 10


def parse_pe(data):
    """最小 PE 解析，不依赖 pefile。"""
    if data[:2] != b'MZ':
        raise SystemExit('[-] not a PE file (missing MZ)')
    e_lfanew = struct.unpack_from('<I', data, 0x3C)[0]
    if data[e_lfanew:e_lfanew + 4] != b'PE\0\0':
        raise SystemExit('[-] not a PE file (missing PE signature)')

    num_sections = struct.unpack_from('<H', data, e_lfanew + 6)[0]
    size_of_opt = struct.unpack_from('<H', data, e_lfanew + 20)[0]
    opt = e_lfanew + 24

    magic = struct.unpack_from('<H', data, opt)[0]
    if magic != 0x20B:
        raise SystemExit('[-] only PE32+ (x64) is supported, got magic 0x%X' % magic)

    entry_rva = struct.unpack_from('<I', data, opt + 16)[0]
    image_base = struct.unpack_from('<Q', data, opt + 24)[0]

    # 数据目录第 5 项 = 基址重定位表
    num_dirs = struct.unpack_from('<I', data, opt + 108)[0]
    reloc_rva = reloc_size = 0
    if num_dirs > 5:
        reloc_rva, reloc_size = struct.unpack_from('<II', data, opt + 112 + 5 * 8)

    # 导入相关目录：扁平镜像里没有加载器，这几项非空就说明 IAT 永远不会被填
    dirs = {}
    for idx, key in ((1, 'import'), (12, 'iat'), (13, 'delay')):
        if num_dirs > idx:
            dirs[key] = struct.unpack_from('<II', data, opt + 112 + idx * 8)
        else:
            dirs[key] = (0, 0)

    sections = []
    sh = opt + size_of_opt
    for i in range(num_sections):
        p = sh + i * 40
        name = data[p:p + 8].rstrip(b'\0').decode('latin1')
        virt_size, virt_addr, raw_size, raw_ptr = struct.unpack_from('<IIII', data, p + 8)
        sections.append({
            'name': name,
            'virt_size': virt_size,
            'virt_addr': virt_addr,
            'raw_size': raw_size,
            'raw_ptr': raw_ptr,
        })

    return {
        'entry_rva': entry_rva,
        'image_base': image_base,
        'reloc_rva': reloc_rva,
        'reloc_size': reloc_size,
        'dirs': dirs,
        'sections': sections,
    }


def rva_to_offset(pe, rva):
    for s in pe['sections']:
        if s['virt_addr'] <= rva < s['virt_addr'] + max(s['virt_size'], s['raw_size']):
            return s['raw_ptr'] + (rva - s['virt_addr'])
    return None


def build_stub(stub_offset, orig_base, entry_offset, num_relocs, reloc_table_offset,
               orig5):
    """自重定位引导 stub。

    重定位修补 + 开头 5 字节还原 -> 直接跳到 entry。
    跨 TU 初始化统一由编译器在 entry 函数首部注入的 __memllvm_dispatch_tu_inits() 调度，
    打包器 stub 只负责让内存环境自洽（重定位基址修复），不越俎代庖分发语义逻辑。
    """
    code = bytearray()

    # 1. call $+5; pop rdx  ->  rdx = 当前 RIP
    code.extend(b'\xE8\x00\x00\x00\x00\x5A')
    pop_rdx_off = len(code) - 1

    # sub rdx, (stub_offset + pop_rdx_off)  ->  rdx = 运行时镜像基址
    code.extend(b'\x48\x81\xEA' + struct.pack('<I', stub_offset + pop_rdx_off))

    # 2. r8 = 运行时基址 - 编译期基址
    code.extend(b'\x48\xB8' + struct.pack('<Q', orig_base))   # mov rax, orig_base
    code.extend(b'\x49\x89\xD0')                              # mov r8, rdx
    code.extend(b'\x49\x29\xC0')                              # sub r8, rax

    # 3. 差值为 0 就跳过修补
    jz_idx = len(code)
    code.extend(b'\x74\x00')

    # 4. 遍历重定位表
    if num_relocs > 0:
        code.extend(b'\x4C\x8D\x8A' + struct.pack('<I', reloc_table_offset))  # lea r9, [rdx+off]
        code.extend(b'\x41\xBA' + struct.pack('<I', num_relocs))              # mov r10d, n

        loop_start = len(code)
        code.extend(b'\x41\x8B\x01')      # mov eax, [r9]
        code.extend(b'\x4C\x01\x04\x02')  # add qword ptr [rdx+rax], r8
        code.extend(b'\x49\x83\xC1\x04')  # add r9, 4
        code.extend(b'\x41\xFF\xCA')      # dec r10d
        loop_disp = loop_start - (len(code) + 2)
        code.extend(b'\x75' + struct.pack('b', loop_disp))

    # skip_rebase:
    skip_target = len(code)
    code[jz_idx + 1] = skip_target - (jz_idx + 2)

    # 5. 还原被 jmp 覆盖的开头 5 字节
    orig_dw, orig_b = struct.unpack('<IB', orig5)
    code.extend(b'\xC7\x02' + struct.pack('<I', orig_dw))     # mov dword ptr [rdx], orig_dw
    code.extend(b'\xC6\x42\x04' + struct.pack('<B', orig_b))  # mov byte ptr [rdx+4], orig_b

    # 6. 跳到真正入口
    code.extend(b'\x48\x8D\x82' + struct.pack('<I', entry_offset))  # lea rax, [rdx+entry_off]
    code.extend(b'\xFF\xE0')                                        # jmp rax

    return bytes(code)


def extract(exe_path, out_bin_path):
    with open(exe_path, 'rb') as f:
        exe_data = f.read()

    pe = parse_pe(exe_data)

    # 关键修正：基址取镜像起点（最低的参与节 VA），不是入口点
    packed = [s for s in pe['sections'] if s['name'] in ALL_SECTIONS]
    if not packed:
        raise SystemExit('[-] no packable sections found')
    base_rva = min(s['virt_addr'] for s in packed)

    orig_base = pe['image_base'] + base_rva
    entry_offset = pe['entry_rva'] - base_rva
    if entry_offset < 0:
        raise SystemExit('[-] entry point 0x%X is before image base 0x%X'
                         % (pe['entry_rva'], base_rva))

    print('[*] %s' % exe_path)
    print('    ImageBase      : 0x%X' % pe['image_base'])
    print('    Base RVA       : 0x%X' % base_rva)
    print('    EntryPoint RVA : 0x%X  -> flat offset 0x%X' % (pe['entry_rva'], entry_offset))

    max_rva = base_rva
    for s in packed:
        end = s['virt_addr'] + max(s['virt_size'], s['raw_size'])
        max_rva = max(max_rva, end)
        print('    %-8s RVA=0x%08X VSize=0x%08X RawSize=0x%08X'
              % (s['name'], s['virt_addr'], s['virt_size'], s['raw_size']))

    dropped = [s['name'] for s in pe['sections'] if s['name'] not in ALL_SECTIONS]
    if dropped:
        print('    丢弃的节: %s' % ', '.join(dropped))

    # ---- 导入表检查 ----------------------------------------------------
    # 扁平镜像没有 PE 加载器，没人填 IAT。导入目录非空 = 这个 bin 跑不起来。
    import_size = pe['dirs']['import'][1]
    delay_size = pe['dirs']['delay'][1]
    iat_rva, iat_size = pe['dirs']['iat']
    if import_size or delay_size:
        print('')
        print('[!] ' + '=' * 66)
        print('[!] 这个 exe 仍带导入表：Import=0x%X 字节, DelayImport=0x%X 字节, IAT RVA=0x%X'
              % (import_size, delay_size, iat_rva))
        print('[!] 说明 MemLLVM 插件没有生效（ApiResolvePass 没把 API 调用改写成 PEB 解析）。')
        print('[!] 扁平镜像里没有加载器，IAT 不会被填。')
        print('[!] 所以这个 .bin 能生成，但加载后第一次调用 Win32 API 就会跳到 0 崩掉。')
        print('[!] 想要真正能跑的 bin：msbuild ... /p:EnableMemLLVM=true')
        print('[!] ' + '=' * 66)
        print('')
    else:
        print('    导入表: 无（API 已全部改为 PEB/ROR13 动态解析）')

    total_size = max_rva - base_rva
    flat = bytearray(total_size)

    for s in packed:
        if s['name'] in BSS_SECTIONS:
            continue  # 未初始化数据：flat 已经是 0
        off = s['virt_addr'] - base_rva
        raw = exe_data[s['raw_ptr']:s['raw_ptr'] + s['raw_size']]
        flat[off:off + len(raw)] = raw

    # 收集 DIR64 重定位
    relocs = []
    if pe['reloc_rva'] and pe['reloc_size']:
        pos = rva_to_offset(pe, pe['reloc_rva'])
        end = pos + pe['reloc_size']
        while pos is not None and pos + 8 <= end:
            page_rva, block_size = struct.unpack_from('<II', exe_data, pos)
            if block_size < 8:
                break
            n = (block_size - 8) // 2
            for i in range(n):
                entry = struct.unpack_from('<H', exe_data, pos + 8 + i * 2)[0]
                if (entry >> 12) == IMAGE_REL_BASED_DIR64:
                    off = (page_rva + (entry & 0xFFF)) - base_rva
                    if 0 <= off < total_size:
                        relocs.append(off)
            pos += block_size

    print('    DIR64 relocations: %d' % len(relocs))

    if not relocs:
        print('[!] 警告：没有重定位项。镜像只能在编译基址上运行。')

    orig5 = bytes(flat[:5])

    stub_offset = (len(flat) + 15) & ~15
    flat.extend(b'\0' * (stub_offset - len(flat)))

    dummy = build_stub(stub_offset, orig_base, entry_offset, len(relocs), 0, orig5)
    reloc_table_offset = stub_offset + len(dummy)
    stub = build_stub(stub_offset, orig_base, entry_offset, len(relocs), reloc_table_offset,
                      orig5)
    reloc_bytes = struct.pack('<%dI' % len(relocs), *relocs) if relocs else b''

    # 开头 5 字节改成 jmp stub
    flat[:5] = struct.pack('<BI', 0xE9, stub_offset - 5)
    flat.extend(stub + reloc_bytes)

    with open(out_bin_path, 'wb') as f:
        f.write(flat)

    print('[+] wrote %s' % out_bin_path)
    print('    stub @ 0x%X, reloc table @ 0x%X, entry @ 0x%X, total %d bytes'
          % (stub_offset, reloc_table_offset, entry_offset, len(flat)))


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print('usage: python memllvm_pack.py <input.exe> <output.bin>')
        sys.exit(1)
    extract(sys.argv[1], sys.argv[2])
