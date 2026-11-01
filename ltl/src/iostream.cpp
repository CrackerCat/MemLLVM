// ============================================================================
// MemLLVM-LTL Iostream Runtime (std::cout, std::cerr for Flat Machine Code)
// Pure Freestanding Console Stream Implementation (Zero-RTTI, Zero-Locale)
// ============================================================================

#include <windows.h>
#include <cstdint>
#include <locale>

#pragma bss_seg(".data")
#pragma data_seg(".data")

struct _MemLLVM_Streambuf_Storage {
    void*  vftable;    // +0 (0x00)
    char*  gfirst;     // +8 (0x08)
    char*  pfirst;     // +16 (0x10)
    char** igfirst;    // +24 (0x18)
    char** ipfirst;    // +32 (0x20)
    char*  gnext;      // +40 (0x28)
    char*  pnext;      // +48 (0x30)
    char** ignext;     // +56 (0x38)
    char** ipnext;     // +64 (0x40)
    int    gcount;     // +72 (0x48)
    int    pcount;     // +76 (0x4C)
    int*   igcount;    // +80 (0x50)
    int*   ipcount;    // +88 (0x58)
    void*  plocale;    // +96 (0x60)
};

static _MemLLVM_Streambuf_Storage _g_cout_buf = { 0 };
static _MemLLVM_Streambuf_Storage _g_cin_buf = { 0 };

extern "C" {

static long long __cdecl _MemLLVM_streambuf_xsputn(void*, const char* s, long long count) {
    if (s && count > 0) {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD written = 0;
        WriteFile(hOut, s, static_cast<DWORD>(count), &written, nullptr);
    }
    return count;
}

static long long __cdecl _MemLLVM_streambuf_xsgetn(void*, char* s, long long count) {
    if (s && count > 0) {
        HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
        DWORD read = 0;
        if (ReadFile(hIn, s, static_cast<DWORD>(count), &read, nullptr)) {
            return read;
        }
    }
    return 0;
}

static char _g_cin_buffer[256] = { 0 };

static int __cdecl _MemLLVM_streambuf_overflow(void*, int ch) {
    if (ch != -1) {
        char c = static_cast<char>(ch);
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD written = 0;
        WriteFile(hOut, &c, 1, &written, nullptr);
    }
    return ch;
}

static int __cdecl _MemLLVM_streambuf_underflow(void* self) {
    _MemLLVM_Streambuf_Storage* buf = static_cast<_MemLLVM_Streambuf_Storage*>(self);
    if (!buf) buf = &_g_cin_buf;
    if (buf->gcount > 0 && buf->gnext) {
        return static_cast<unsigned char>(*buf->gnext);
    }
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD bytesRead = 0;
    if (!ReadFile(hIn, _g_cin_buffer, sizeof(_g_cin_buffer), &bytesRead, nullptr) || bytesRead == 0) {
        return -1;
    }
    buf->gfirst = _g_cin_buffer;
    buf->gnext = _g_cin_buffer;
    buf->gcount = static_cast<int>(bytesRead);
    return static_cast<unsigned char>(*buf->gnext);
}

static int __cdecl _MemLLVM_streambuf_uflow(void* self) {
    int ch = _MemLLVM_streambuf_underflow(self);
    if (ch == -1) return -1;
    _MemLLVM_Streambuf_Storage* buf = static_cast<_MemLLVM_Streambuf_Storage*>(self);
    if (!buf) buf = &_g_cin_buf;
    buf->gnext++;
    buf->gcount--;
    return ch;
}

static int __cdecl _MemLLVM_streambuf_noop(void*) {
    return 0;
}

} // extern "C"

namespace std {

static void* _g_streambuf_vtable[16] = { 0 };

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
    void* rdbuf;               // +88 (0x58) - points to streambuf
    void* tied;                // +96 (0x60) - nullptr
    long long fillch;          // +104 (0x68) - ' ' (0x20)
};

static_assert(sizeof(_MemLLVM_Ostream_Storage) == 112, "ostream layout size mismatch");

_MemLLVM_Ostream_Storage _g_cout_storage = { 0 };
_MemLLVM_Ostream_Storage _g_cerr_storage = { 0 };
_MemLLVM_Ostream_Storage _g_cin_storage = { 0 };

static void* _g_cin_locale_storage[2] = { 0 };
static void* _g_cout_locale_storage[2] = { 0 };

extern "C" void* MemLLVM_GetClassicLocimp();

} // namespace std

extern "C" void MemLLVM_InitCout(void) {
    using namespace std;

    for (int i = 0; i < 16; ++i) {
        _g_streambuf_vtable[i] = (void*)&_MemLLVM_streambuf_noop;
    }
    _g_streambuf_vtable[3] = (void*)&_MemLLVM_streambuf_overflow;
    _g_streambuf_vtable[6] = (void*)&_MemLLVM_streambuf_underflow;
    _g_streambuf_vtable[7] = (void*)&_MemLLVM_streambuf_uflow;
    _g_streambuf_vtable[8] = (void*)&_MemLLVM_streambuf_xsgetn;
    _g_streambuf_vtable[9] = (void*)&_MemLLVM_streambuf_xsputn;

    _g_cout_buf.vftable = _g_streambuf_vtable;
    _g_cout_buf.igfirst = &_g_cout_buf.gfirst;
    _g_cout_buf.ipfirst = &_g_cout_buf.pfirst;
    _g_cout_buf.ignext  = &_g_cout_buf.gnext;
    _g_cout_buf.ipnext  = &_g_cout_buf.pnext;
    _g_cout_buf.igcount = &_g_cout_buf.gcount;
    _g_cout_buf.ipcount = &_g_cout_buf.pcount;

    _g_cin_buf.vftable = _g_streambuf_vtable;
    _g_cin_buf.igfirst = &_g_cin_buf.gfirst;
    _g_cin_buf.ipfirst = &_g_cin_buf.pfirst;
    _g_cin_buf.ignext  = &_g_cin_buf.gnext;
    _g_cin_buf.ipnext  = &_g_cin_buf.pnext;
    _g_cin_buf.igcount = &_g_cin_buf.gcount;
    _g_cin_buf.ipcount = &_g_cin_buf.pcount;

    void* locimp = MemLLVM_GetClassicLocimp();
    _g_cout_locale_storage[1] = locimp;
    _g_cin_locale_storage[1] = locimp;

    _g_cout_storage.vbtable = _g_cout_vbtable;
    _g_cout_storage.mystate = 0;
    _g_cout_storage.fmtfl = 0x201;
    _g_cout_storage.prec = 6;
    _g_cout_storage.ploc = _g_cout_locale_storage;
    _g_cout_storage.rdbuf = &_g_cout_buf;
    _g_cout_storage.fillch = 0x20;

    _g_cerr_storage.vbtable = _g_cout_vbtable;
    _g_cerr_storage.mystate = 0;
    _g_cerr_storage.fmtfl = 0x201;
    _g_cerr_storage.prec = 6;
    _g_cerr_storage.ploc = _g_cout_locale_storage;
    _g_cerr_storage.rdbuf = &_g_cout_buf;
    _g_cerr_storage.fillch = 0x20;

    _g_cin_storage.vbtable = _g_cout_vbtable;
    _g_cin_storage.mystate = 0;
    _g_cin_storage.fmtfl = 0x201;
    _g_cin_storage.prec = 6;
    _g_cin_storage.ploc = _g_cin_locale_storage;
    _g_cin_storage.rdbuf = &_g_cin_buf;
    _g_cin_storage.fillch = 0x20;
}

#pragma comment(linker, "/alternatename:?cout@std@@3V?$basic_ostream@DU?$char_traits@D@std@@@1@A=?_g_cout_storage@std@@3U_MemLLVM_Ostream_Storage@1@A")
#pragma comment(linker, "/alternatename:?cerr@std@@3V?$basic_ostream@DU?$char_traits@D@std@@@1@A=?_g_cerr_storage@std@@3U_MemLLVM_Ostream_Storage@1@A")
#pragma comment(linker, "/alternatename:?cin@std@@3V?$basic_istream@DU?$char_traits@D@std@@@1@A=?_g_cin_storage@std@@3U_MemLLVM_Ostream_Storage@1@A")
