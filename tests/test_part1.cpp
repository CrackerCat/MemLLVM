#include <windows.h>
#include <cstdint>

extern "C" {
static long long __cdecl _MemLLVM_streambuf_xsputn(void*, const char* s, long long count) {
    if (s && count > 0) {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD written = 0;
        WriteFile(hOut, s, static_cast<DWORD>(count), &written, nullptr);
    }
    return count;
}

static int __cdecl _MemLLVM_streambuf_overflow(void*, int ch) {
    if (ch != -1) {
        char c = static_cast<char>(ch);
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD written = 0;
        WriteFile(hOut, &c, 1, &written, nullptr);
    }
    return ch;
}

static int __cdecl _MemLLVM_streambuf_noop(void*) { return 0; }
}

void dummy_entry() {}
