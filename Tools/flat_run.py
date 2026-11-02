#!/usr/bin/env python3
"""
Minimal flat binary runner (for validation and automated testing).

Maps the position-independent .bin into RWX memory and invokes offset 0.
Offset 0 starts with the self-rebase stub (which applies DIR64 relocations,
restores the clobbered 5 bytes, and jumps to entry).
Cross-TU initializers in .mli are dispatched at the entry point by the compiler.

Usage: python flat_run.py <bin>
"""

import ctypes
import sys

MEM_COMMIT_RESERVE = 0x3000
PAGE_EXECUTE_READWRITE = 0x40


def main():
    if len(sys.argv) < 2:
        print('usage: python flat_run.py <bin> [expected_rc]')
        return 1

    expected_rc = int(sys.argv[2]) if len(sys.argv) >= 3 else None

    with open(sys.argv[1], 'rb') as f:
        image = f.read()

    k32 = ctypes.WinDLL('kernel32', use_last_error=True)
    k32.VirtualAlloc.restype = ctypes.c_void_p
    k32.VirtualAlloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                                 ctypes.c_uint32, ctypes.c_uint32]

    size = len(image) + 0x1000
    base = k32.VirtualAlloc(None, size, MEM_COMMIT_RESERVE, PAGE_EXECUTE_READWRITE)
    if not base:
        print('[-] VirtualAlloc failed: %d' % ctypes.get_last_error())
        return 1

    ctypes.memmove(base, image, len(image))
    print('[*] image %d bytes mapped at 0x%X' % (len(image), base))

    entry = ctypes.CFUNCTYPE(ctypes.c_int)(base)
    rc = entry()
    print('[*] returned %d' % rc)
    if expected_rc is not None:
        return 0 if rc == expected_rc else 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
