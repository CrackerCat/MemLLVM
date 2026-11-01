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

namespace std {
static void* _g_streambuf_vtable[16] = { 0 };
struct _MemLLVM_Streambuf_Storage {
    void* vftable;
    char dummy[128];
};
static _MemLLVM_Streambuf_Storage _g_cout_buf = { 0 };
static const int _g_cout_vbtable[2] = { 0, 16 };
struct _MemLLVM_Ostream_Storage {
    const int* vbtable;
    void* padding1;
    void* ios_vftable;
    void* padding2;
    int   mystate;
    int   padding3;
    long long fmtfl;
    long long prec;
    long long wide;
    void* arr;
    void* calls;
    void* ploc;
    void* rdbuf;
    void* tied;
    long long fillch;
};
_MemLLVM_Ostream_Storage _g_cout_storage = { 0 };
_MemLLVM_Ostream_Storage _g_cerr_storage = { 0 };
}

extern "C" void MemLLVM_InitCout(void) {
    using namespace std;
    _g_streambuf_vtable[0] = (void*)&_MemLLVM_streambuf_noop;
    _g_streambuf_vtable[1] = (void*)&_MemLLVM_streambuf_overflow;
    _g_streambuf_vtable[2] = (void*)&_MemLLVM_streambuf_noop;
    _g_streambuf_vtable[3] = (void*)&_MemLLVM_streambuf_xsputn;
    for (int i = 4; i < 16; ++i) {
        _g_streambuf_vtable[i] = (void*)&_MemLLVM_streambuf_noop;
    }
    _g_cout_buf.vftable = _g_streambuf_vtable;
    _g_cout_storage.vbtable = _g_cout_vbtable;
    _g_cout_storage.mystate = 0;
    _g_cout_storage.fmtfl = 0x201;
    _g_cout_storage.prec = 6;
    _g_cout_storage.rdbuf = &_g_cout_buf;
    _g_cout_storage.fillch = 0x20;
    _g_cerr_storage.vbtable = _g_cout_vbtable;
    _g_cerr_storage.mystate = 0;
    _g_cerr_storage.fmtfl = 0x201;
    _g_cerr_storage.prec = 6;
    _g_cerr_storage.rdbuf = &_g_cout_buf;
    _g_cerr_storage.fillch = 0x20;
}
