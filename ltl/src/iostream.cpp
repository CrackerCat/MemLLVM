// ============================================================================
// MemLLVM-LTL Iostream Runtime (std::cout, std::cerr for Flat Machine Code)
// Pure Freestanding Console Stream Implementation (Zero-RTTI, Zero-Locale)
// ============================================================================

#include <windows.h>
#include <cstdint>

#pragma bss_seg(".data")
#pragma data_seg(".data")

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

static int __cdecl _MemLLVM_streambuf_noop(void*) {
    return 0;
}

} // extern "C"

namespace std {

static void* _g_streambuf_vtable[16] = { 0 };

struct _MemLLVM_Streambuf_Storage {
    void* vftable;
    char dummy[128];
};

static _MemLLVM_Streambuf_Storage _g_cout_buf = { 0 };

static const int _g_cout_vbtable[2] = { 0, 16 };

struct _MemLLVM_Ostream_Storage {
    const int* vbtable;        // +0 (0x00)
    void* padding1;            // +8 (0x08)
    void* ios_vftable;         // +16 (0x10)
    void* padding2;            // +24 (0x18)
    int   mystate;             // +32 (0x20) - 0 = goodbit
    int   padding3;            // +36 (0x24)
    long long fmtfl;           // +40 (0x28) - 0x201 (skipws | dec)
    long long prec;            // +48 (0x30) - 6
    long long wide;            // +56 (0x38) - 0
    void* arr;                 // +64 (0x40)
    void* calls;               // +72 (0x48)
    void* ploc;                // +80 (0x50)
    void* rdbuf;               // +88 (0x58) - points to _g_cout_buf
    void* tied;                // +96 (0x60) - nullptr
    long long fillch;          // +104 (0x68) - ' ' (0x20)
};

static_assert(sizeof(_MemLLVM_Ostream_Storage) == 112, "ostream layout size mismatch");

_MemLLVM_Ostream_Storage _g_cout_storage = { 0 };
_MemLLVM_Ostream_Storage _g_cerr_storage = { 0 };

} // namespace std

extern "C" void MemLLVM_InitCout(void) {
    using namespace std;

    for (int i = 0; i < 16; ++i) {
        _g_streambuf_vtable[i] = (void*)&_MemLLVM_streambuf_noop;
    }
    _g_streambuf_vtable[3] = (void*)&_MemLLVM_streambuf_overflow;
    _g_streambuf_vtable[9] = (void*)&_MemLLVM_streambuf_xsputn;

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

#pragma comment(linker, "/alternatename:?cout@std@@3V?$basic_ostream@DU?$char_traits@D@std@@@1@A=?_g_cout_storage@std@@3U_MemLLVM_Ostream_Storage@1@A")
#pragma comment(linker, "/alternatename:?cerr@std@@3V?$basic_ostream@DU?$char_traits@D@std@@@1@A=?_g_cerr_storage@std@@3U_MemLLVM_Ostream_Storage@1@A")
