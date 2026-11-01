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
extern "C" void MemLLVM_InitCout(void) {}
