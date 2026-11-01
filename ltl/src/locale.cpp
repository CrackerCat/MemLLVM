// ============================================================================
// MemLLVM-LTL Locale & Stream Runtime (std::locale, std::fstream, std::cin)
// Zero-Bloat Classic "C" Locale & File Stream Bridge for Flat Machine Code
// ============================================================================

#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <ios>
#include <locale>
#include <xlocinfo>

#pragma bss_seg(".data")
#pragma data_seg(".data")

extern "C" {

FILE* fopen(const char* filename, const char* mode);
FILE* _wfopen(const wchar_t* filename, const wchar_t* mode);

static unsigned short _g_ascii_ctype_table[256] = { 0 };
static bool _g_ascii_table_inited = false;

static void _InitAsciiCtypeTable() {
    if (_g_ascii_table_inited) return;
    _g_ascii_table_inited = true;
    for (int i = 0; i < 256; ++i) {
        unsigned short mask = 0;
        if (i < 32 || i == 127) {
            mask |= 0x20; // _CONTROL
        }
        if (i == ' ' || (i >= 0x09 && i <= 0x0D)) {
            mask |= 0x08; // _SPACE
        }
        if (i == ' ' || i == '\t') {
            mask |= 0x40; // _BLANK
        }
        if (i >= '0' && i <= '9') {
            mask |= 0x04 | 0x80; // _DIGIT | _HEX
        }
        if ((i >= 'a' && i <= 'f') || (i >= 'A' && i <= 'F')) {
            mask |= 0x80; // _HEX
        }
        if (i >= 'A' && i <= 'Z') {
            mask |= 0x01 | 0x0100; // _UPPER | _ALPHA
        }
        if (i >= 'a' && i <= 'z') {
            mask |= 0x02 | 0x0100; // _LOWER | _ALPHA
        }
        if ((i >= 33 && i <= 47) || (i >= 58 && i <= 64) ||
            (i >= 91 && i <= 96) || (i >= 123 && i <= 126)) {
            mask |= 0x10; // _PUNCT
        }
        _g_ascii_ctype_table[i] = mask;
    }
}

_Ctypevec __cdecl _Getctype() noexcept {
    _InitAsciiCtypeTable();
    _Ctypevec vec;
    vec._Page = 0;
    vec._Table = (const short*)_g_ascii_ctype_table;
    vec._Delfl = 0;
    vec._LocaleName = nullptr;
    return vec;
}

int __cdecl _Tolower(int c, const _Ctypevec*) noexcept {
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

int __cdecl _Toupper(int c, const _Ctypevec*) noexcept {
    if (c >= 'a' && c <= 'z') return c - ('a' - 'A');
    return c;
}

_Collvec __cdecl _Getcoll() noexcept {
    _Collvec vec = { 0, nullptr };
    return vec;
}

_Cvtvec __cdecl _Getcvt() noexcept {
    _Cvtvec vec = { 0 };
    vec._Mbcurmax = 1;
    vec._Isclocale = 1;
    return vec;
}

int __cdecl _Strcoll(const char* s1, const char* e1, const char* s2, const char* e2, const _Collvec*) noexcept {
    size_t len1 = e1 - s1;
    size_t len2 = e2 - s2;
    size_t minlen = len1 < len2 ? len1 : len2;
    int cmp = memcmp(s1, s2, minlen);
    if (cmp != 0) return cmp;
    return (len1 < len2) ? -1 : (len1 > len2 ? 1 : 0);
}

size_t __cdecl _Strxfrm(char* s1, char* e1, const char* s2, const char* e2, const _Collvec*) noexcept {
    size_t len2 = e2 - s2;
    size_t maxlen = e1 - s1;
    size_t copylen = len2 < maxlen ? len2 : maxlen;
    memcpy(s1, s2, copylen);
    return len2;
}

int __cdecl _Wcscoll(const wchar_t* s1, const wchar_t* e1, const wchar_t* s2, const wchar_t* e2, const _Collvec*) noexcept {
    size_t len1 = e1 - s1;
    size_t len2 = e2 - s2;
    size_t minlen = len1 < len2 ? len1 : len2;
    for (size_t i = 0; i < minlen; ++i) {
        if (s1[i] != s2[i]) return (s1[i] < s2[i]) ? -1 : 1;
    }
    return (len1 < len2) ? -1 : (len1 > len2 ? 1 : 0);
}

size_t __cdecl _Wcsxfrm(wchar_t* s1, wchar_t* e1, const wchar_t* s2, const wchar_t* e2, const _Collvec*) noexcept {
    size_t len2 = e2 - s2;
    size_t maxlen = e1 - s1;
    size_t copylen = len2 < maxlen ? len2 : maxlen;
    for (size_t i = 0; i < copylen; ++i) s1[i] = s2[i];
    return len2;
}

short __cdecl _Getwctype(wchar_t c, const _Ctypevec*) noexcept {
    if (c < 256) {
        _InitAsciiCtypeTable();
        return (short)_g_ascii_ctype_table[(unsigned char)c];
    }
    return 0;
}

const wchar_t* __cdecl _Getwctypes(const wchar_t* first, const wchar_t* last, short* res, const _Ctypevec*) noexcept {
    _InitAsciiCtypeTable();
    for (; first != last; ++first, ++res) {
        if (*first < 256) {
            *res = (short)_g_ascii_ctype_table[(unsigned char)*first];
        } else {
            *res = 0;
        }
    }
    return first;
}

wchar_t __cdecl _Towlower(wchar_t c, const _Ctypevec*) noexcept {
    if (c >= L'A' && c <= L'Z') return c + (L'a' - L'A');
    return c;
}

wchar_t __cdecl _Towupper(wchar_t c, const _Ctypevec*) noexcept {
    if (c >= L'a' && c <= L'z') return c - (L'a' - L'A');
    return c;
}

int __cdecl _Mbrtowc(wchar_t* pwc, const char* s, size_t n, mbstate_t*, const _Cvtvec*) noexcept {
    if (!s || n == 0) return 0;
    if (pwc) *pwc = (wchar_t)(unsigned char)*s;
    return (*s == 0) ? 0 : 1;
}

int __cdecl _Wcrtomb(char* s, wchar_t wc, mbstate_t*, const _Cvtvec*) noexcept {
    if (!s) return 1;
    *s = (char)wc;
    return 1;
}

struct _CRT_FILE_64 {
    char* _ptr;
    int   _cnt;
    char* _base;
    int   _flag;
    int   _file;
    int   _charbuf;
    int   _bufsiz;
    char* _tmpfname;
};

int __cdecl _get_stream_buffer_pointers(FILE* stream, char*** base, char*** ptr, int** count) noexcept {
    if (!stream) return 22; // EINVAL
    _CRT_FILE_64* f = (_CRT_FILE_64*)stream;
    if (base) *base = &f->_base;
    if (ptr) *ptr = &f->_ptr;
    if (count) *count = &f->_cnt;
    return 0;
}

void __cdecl _lock_file(FILE*) noexcept {}
void __cdecl _unlock_file(FILE*) noexcept {}

} // extern "C"

namespace std {

// 1. _Lockit Implementation (No-op in freestanding flat binary)
_Lockit::_Lockit() noexcept {}
_Lockit::_Lockit(int) noexcept {}
_Lockit::~_Lockit() noexcept {}
void __cdecl _Lockit::_Lockit_ctor(int) noexcept {}
void __cdecl _Lockit::_Lockit_dtor(int) noexcept {}
void __cdecl _Lockit::_Lockit_ctor(_Lockit*) noexcept {}
void __cdecl _Lockit::_Lockit_ctor(_Lockit*, int) noexcept {}
void __cdecl _Lockit::_Lockit_dtor(_Lockit*) noexcept {}

// 2. _Locinfo Implementation
void __cdecl _Locinfo::_Locinfo_ctor(_Locinfo*, const char*) {}
void __cdecl _Locinfo::_Locinfo_ctor(_Locinfo*, int, const char*) {}
void __cdecl _Locinfo::_Locinfo_dtor(_Locinfo*) {}
_Locinfo& __cdecl _Locinfo::_Locinfo_Addcats(_Locinfo* loc, int, const char*) {
    return *loc;
}

// 3. Facet Registration
void __cdecl _Facet_Register(_Facet_base*) {}

// 4. Static Facet IDs
int locale::id::_Id_cnt = 1;

locale::id ctype<char>::id;
locale::id ctype<wchar_t>::id;
template<> locale::id collate<char>::id;
template<> locale::id collate<wchar_t>::id;
template<> locale::id codecvt<char, char, _Mbstatet>::id;
locale::id codecvt<wchar_t, char, _Mbstatet>::id;

// 4.1. Classic "C" Locale Implementation (_Locimp)
static void* _g_locimp_vftable[4] = { 0 };
static void __cdecl _dummy_incref(void*) {}
static void* __cdecl _dummy_decref(void*) { return nullptr; }

struct _MemLLVM_Locimp_Storage {
    void* vftable;
    int   refs;
    int   padding;
    locale::facet** facetvec;
    size_t facetcount;
    int   catmask;
    bool  xparent;
    char  padding2[3];
    const char* name;
    size_t name_len;
    size_t name_res;
};

static _MemLLVM_Locimp_Storage _g_clocimp = { 0 };

extern "C" void* MemLLVM_GetClassicLocimp() {
    if (!_g_locimp_vftable[1]) {
        _g_locimp_vftable[0] = (void*)&_dummy_decref;
        _g_locimp_vftable[1] = (void*)&_dummy_incref;
        _g_locimp_vftable[2] = (void*)&_dummy_decref;

        _g_clocimp.vftable = _g_locimp_vftable;
        _g_clocimp.refs = 9999;
        _g_clocimp.facetvec = nullptr;
        _g_clocimp.facetcount = 0;
        _g_clocimp.catmask = 0;
        _g_clocimp.xparent = false;
        _g_clocimp.name = "C";
    }
    return (void*)&_g_clocimp;
}

locale::_Locimp* __cdecl locale::_Init(bool) {
    return (locale::_Locimp*)MemLLVM_GetClassicLocimp();
}

locale::_Locimp* __cdecl locale::_Getgloballocale() {
    return (locale::_Locimp*)MemLLVM_GetClassicLocimp();
}

// 5. ios_base cleanups
void __cdecl ios_base::_Ios_base_dtor(ios_base*) {}
void __cdecl ios_base::_Addstd(ios_base*) {}

// 6. _Fiopen (Bridge fstream to CRT/Win32 fopen)
FILE* __cdecl _Fiopen(const char* name, ios_base::openmode mode, int) {
    char smode[8] = { 0 };
    int idx = 0;

    if (mode & ios_base::out) {
        if (mode & ios_base::app) {
            smode[idx++] = 'a';
        } else if (mode & ios_base::trunc) {
            smode[idx++] = 'w';
        } else {
            smode[idx++] = (mode & ios_base::in) ? 'r' : 'w';
        }
        if (mode & ios_base::in) {
            smode[idx++] = '+';
        }
    } else if (mode & ios_base::in) {
        smode[idx++] = 'r';
    }

    if (mode & ios_base::binary) {
        smode[idx++] = 'b';
    }

    smode[idx] = '\0';
    return fopen(name, smode[0] ? smode : "r");
}

FILE* __cdecl _Fiopen(const wchar_t* name, ios_base::openmode mode, int) {
    wchar_t smode[8] = { 0 };
    int idx = 0;

    if (mode & ios_base::out) {
        if (mode & ios_base::app) {
            smode[idx++] = L'a';
        } else if (mode & ios_base::trunc) {
            smode[idx++] = L'w';
        } else {
            smode[idx++] = (mode & ios_base::in) ? L'r' : L'w';
        }
        if (mode & ios_base::in) {
            smode[idx++] = L'+';
        }
    } else if (mode & ios_base::in) {
        smode[idx++] = L'r';
    }

    if (mode & ios_base::binary) {
        smode[idx++] = L'b';
    }

    smode[idx] = L'\0';
    return _wfopen(name, smode[0] ? smode : L"r");
}

} // namespace std
