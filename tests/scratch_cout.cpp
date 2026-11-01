#include <windows.h>
#include <streambuf>

namespace std {

class _MemLLVM_ConsoleBuf : public streambuf {
public:
    _MemLLVM_ConsoleBuf() {
        // streambuf vtable is initialized
    }
protected:
    int_type overflow(int_type ch) override {
        if (ch != traits_type::eof()) {
            char c = static_cast<char>(ch);
            HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
            DWORD written = 0;
            WriteFile(hOut, &c, 1, &written, nullptr);
        }
        return ch;
    }

    streamsize xsputn(const char* s, streamsize count) override {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD written = 0;
        WriteFile(hOut, s, static_cast<DWORD>(count), &written, nullptr);
        return count;
    }

    int sync() override {
        return 0;
    }
};



static const int _g_cout_vbtable[2] = { 0, 16 };
static _MemLLVM_ConsoleBuf _g_cout_buf;

struct _MemLLVM_Ostream_Storage {
    const int* vbtable;        // +0 (0x00)
    void* padding1;            // +8 (0x08)
    void* ios_vftable;         // +16 (0x10)
    void* padding2;            // +24 (0x18)
    int   mystate;             // +32 (0x20) - 0 = goodbit
    int   padding3;            // +36 (0x24)
    long long fmtfl;           // +40 (0x28) - 0x201
    long long prec;            // +48 (0x30) - 6
    long long wide;            // +56 (0x38) - 0
    void* arr;                 // +64 (0x40)
    void* calls;               // +72 (0x48)
    void* ploc;                // +80 (0x50)
    streambuf* rdbuf;          // +88 (0x58) - points to _g_cout_buf
    void* tied;                // +96 (0x60) - nullptr
    long long fillch;          // +104 (0x68) - ' ' (0x20)
};

_MemLLVM_Ostream_Storage _g_cout_storage = { 0 };

} // namespace std

extern "C" void MemLLVM_InitCout(void) {
    using namespace std;
    // Placement new / re-init streambuf vtable
    new (&_g_cout_buf) _MemLLVM_ConsoleBuf();

    _g_cout_storage.vbtable = _g_cout_vbtable;
    _g_cout_storage.mystate = 0;
    _g_cout_storage.fmtfl = 0x201;
    _g_cout_storage.prec = 6;
    _g_cout_storage.rdbuf = &_g_cout_buf;
    _g_cout_storage.fillch = 0x20;
}

#pragma comment(linker, "/alternatename:?cout@std@@3V?$basic_ostream@DU?$char_traits@D@std@@@1@A=?_g_cout_storage@std@@3U_MemLLVM_Ostream_Storage@1@A")
#pragma comment(linker, "/alternatename:?cerr@std@@3V?$basic_ostream@DU?$char_traits@D@std@@@1@A=?_g_cout_storage@std@@3U_MemLLVM_Ostream_Storage@1@A")
