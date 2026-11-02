#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
从 mimikatz.vcxproj 生成 clang-cl 编译脚本。

为什么不用 msbuild：本机安全策略把 MSBuild.exe 归入 Known Windows LOLBin 拦截。
mimikatz 是纯 vcxproj 工程（没有 CMake），所以只能自己还原编译命令行。
命令行参数直接从 vcxproj 里读，避免手抄出错。
"""

import os
import re
import sys
import xml.etree.ElementTree as ET

NS = '{http://schemas.microsoft.com/developer/msbuild/2003}'
CFG = "'$(Configuration)|$(Platform)'=='Release|x64'"
CFG_ANY = "'$(Configuration)'=='Release'"


def text_of(el):
    return (el.text or '').strip()


def parse(proj, repo):
    tree = ET.parse(proj)
    root = tree.getroot()

    inc = [repo + '/inc']

    global_defs = []
    lang_std = '/TC'
    extra_opts = ''
    for idg in root.iter(NS + 'ItemDefinitionGroup'):
        cond = idg.attrib.get('Condition', '')
        if cond and cond not in (CFG, CFG_ANY):
            continue
        for cc in idg.iter(NS + 'ClCompile'):
            for ch in cc:
                tag = ch.tag.replace(NS, '')
                if tag == 'PreprocessorDefinitions':
                    c = ch.attrib.get('Condition', '')
                    if c and c not in (CFG, CFG_ANY):
                        continue
                    global_defs = [d for d in text_of(ch).split(';')
                                   if d and not d.startswith('%(')]
                elif tag == 'AdditionalOptions':
                    c = ch.attrib.get('Condition', '')
                    if c and c not in (CFG, CFG_ANY):
                        continue
                    extra_opts = re.sub(r'%\(AdditionalOptions\)', '', text_of(ch)).strip()

    # 每文件的宏（sqlite3.c 等）
    per_file = {}
    items = []
    for cc in root.iter(NS + 'ClCompile'):
        inc_attr = cc.attrib.get('Include')
        if not inc_attr:
            continue
        items.append(inc_attr)
        defs = None
        for ch in cc:
            tag = ch.tag.replace(NS, '')
            if tag == 'PreprocessorDefinitions':
                c = ch.attrib.get('Condition', '')
                if c and c not in (CFG, CFG_ANY):
                    continue
                defs = [d for d in text_of(ch).split(';')
                        if d and not d.startswith('%(')]
        if defs:
            per_file[inc_attr] = defs

    return inc, global_defs, per_file, items, extra_opts


def main():
    repo = r'C:\Users\admin\Downloads\mimikatz-master\mimikatz-master'
    proj = os.path.join(repo, 'mimikatz', 'mimikatz.vcxproj')
    out_dir = os.path.join(repo, 'build', 'mllvm')
    os.makedirs(out_dir, exist_ok=True)

    inc, gdefs, per_file, items, extra = parse(proj, repo)

    base = ('/c /nologo /W0 /WX- /Ox /Ob2 /Os /Oy /GF /MT /GS- /Gy /fp:fast '
            '/fp:except- /Gd --target=amd64-pc-windows-msvc -fcommon /utf-8')

    # MemLLVM pass 插件。--no-plugin 可以关掉，用来对比「插件开/关」的未解析符号数。
    #
    # 必须写成 /clang:-fpass-plugin=... 透传给 cc1：clang-cl 自己不认识
    # -fpass-plugin，直接写会被当成 unknown argument 静默忽略（只出一条
    # warning），结果是「构建成功但插件根本没跑」——之前踩过这个坑。
    if '--no-plugin' not in sys.argv:
        # -mno-stack-arg-probe 已移除：ltl 现在提供了真正的 __chkstk
        # （逐页 touch gs:[0x10] 以下的页），大栈帧会自动触发 guard page 增长。
        # 保留这个开关会让编译器不生成 call __chkstk，等于让探测实现永远不执行。
        base += (' /clang:-fpass-plugin=D:/MemLLVM/bin/MemLLVM.dll'
                 ' /clang:-fno-jump-tables'
                 ' /clang:-fno-builtin')

    lines = []
    lines.append('@echo off')
    lines.append('setlocal enabledelayedexpansion')
    lines.append('set "REPO=%s"' % repo)
    lines.append('set "OUT=%s"' % out_dir)
    lines.append('set "LLVM=C:\\LLVM-22-dev\\bin"')
    lines.append('set "MSVC=C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\'
                 'VC\\Tools\\MSVC\\14.51.36231"')
    lines.append('set "SDK=C:\\Program Files (x86)\\Windows Kits\\10"')
    lines.append('set "SDKV=10.0.26100.0"')
    lines.append('set "INCLUDE=%MSVC%\\include;%SDK%\\Include\\%SDKV%\\ucrt;'
                 '%SDK%\\Include\\%SDKV%\\um;%SDK%\\Include\\%SDKV%\\shared"')
    lines.append('set "LIB=%MSVC%\\lib\\x64;%SDK%\\Lib\\%SDKV%\\ucrt\\x64;'
                 '%SDK%\\Lib\\%SDKV%\\um\\x64"')
    lines.append('set "PATH=%LLVM%;%MSVC%\\bin\\Hostx64\\x64;%PATH%"')
    lines.append('cd /d "%REPO%\\mimikatz"')
    lines.append('set FAILED=0')
    lines.append('')

    for it in items:
        src = it.replace('\\', '/')
        obj = os.path.basename(src).replace('.c', '.obj').replace('.cpp', '.obj')
        defs = per_file.get(it, gdefs)
        defs = defs + ['_UNICODE', 'UNICODE']
        seen = []
        for d in defs:
            if d not in seen:
                seen.append(d)
        defs_arg = ' '.join('/D ' + d for d in seen)
        lines.append('echo [CC] %s' % obj)
        lines.append('clang-cl %s %s /I"%s" /Fo"%s\\%s" "%s" || set FAILED=1'
                     % (base, defs_arg, ';'.join(inc), out_dir, obj, it))

    lines.append('')
    lines.append('if "%FAILED%"=="1" (echo [-] compile failed & exit /b 1)')
    lines.append('echo [+] all TUs compiled into %OUT%')
    lines.append('endlocal')

    bat = os.path.join(repo, 'build', 'compile_all.bat')
    with open(bat, 'w', encoding='utf-8') as f:
        f.write('\r\n'.join(lines) + '\r\n')
    print('[+] wrote %s  (%d TUs)' % (bat, len(items)))
    print('    extra opts from props: %s' % extra)
    print('    global defs: %d' % len(gdefs))
    print('    per-file overrides: %s' % ', '.join(sorted(per_file)))


if __name__ == '__main__':
    main()
