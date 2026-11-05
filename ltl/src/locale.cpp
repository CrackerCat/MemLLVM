// ============================================================================
// MemLLVM-LTL Locale & Stream Runtime (std::locale, std::fstream, std::cin)
// Zero-Bloat Classic "C" Locale & File Stream Bridge for Flat Machine Code
// ============================================================================

#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <climits>
#include <cstdlib>
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
void __cdecl _Locinfo::_Locinfo_ctor(_Locinfo* pLocinfo, const char* locname) {
    pLocinfo->_Newlocname = (locname && locname[0]) ? locname : "C";
}
void __cdecl _Locinfo::_Locinfo_ctor(_Locinfo* pLocinfo, int, const char* locname) {
    pLocinfo->_Newlocname = (locname && locname[0]) ? locname : "C";
}
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

// 4.2. Named Locale Creation & Management (_New_Locimp, _Makeloc)
locale::_Locimp* __cdecl locale::_Locimp::_New_Locimp(bool _Transparent) {
    return new _Locimp(_Transparent);
}

locale::_Locimp* __cdecl locale::_Locimp::_New_Locimp(const _Locimp& _Right) {
    return new _Locimp(_Right);
}

void __cdecl locale::_Locimp::_Locimp_ctor(locale::_Locimp* _This, const locale::_Locimp& imp) {
    _This->_Facetvec = nullptr;
    _This->_Facetcount = imp._Facetcount;
    _This->_Catmask = imp._Catmask;
    _This->_Xparent = imp._Xparent;
    if (0 < _This->_Facetcount && imp._Facetvec) {
        _This->_Facetvec = static_cast<locale::facet**>(malloc(_This->_Facetcount * sizeof(locale::facet*)));
        if (_This->_Facetvec) {
            for (size_t count = _This->_Facetcount; 0 < count;) {
                locale::facet* ptrfac = imp._Facetvec[--count];
                if ((_This->_Facetvec[count] = ptrfac) != nullptr) {
                    ptrfac->_Incref();
                }
            }
        }
    }
}

void __cdecl locale::_Locimp::_Locimp_dtor(locale::_Locimp* _This) {
    for (size_t count = _This->_Facetcount; 0 < count;) {
        if (_This->_Facetvec && _This->_Facetvec[--count] != nullptr) {
            delete _This->_Facetvec[count]->_Decref();
        }
    }
    if (_This->_Facetvec) {
        free(_This->_Facetvec);
        _This->_Facetvec = nullptr;
    }
}

locale::_Locimp* __cdecl locale::_Locimp::_Makeloc(
    const _Locinfo& _Lobj, category _Cat, _Locimp* _Pimp, const locale*) {
    _Pimp->_Catmask |= _Cat;
    _Pimp->_Name = _Lobj._Getname();
    return _Pimp;
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

// ============================================================================
// MSVC STL String-to-Number Parsing & Buffer Reversal Primitives
// Satisfies std::istream >> [int|float|double], xlocnum, and vector algorithms
// ============================================================================

extern "C" {

unsigned long long __cdecl _Stoullx(
    const char* s, char** endptr, int base, int* perr) noexcept {
    constexpr int _Base_max = 36;
    static constexpr char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    static constexpr char ndigs[_Base_max + 1] = {
        0, 0, 65, 41, 33, 28, 25, 23, 22, 21, 20, 19, 18, 18, 17, 17, 17, 16,
        16, 16, 15, 15, 15, 15, 14, 14, 14, 14, 14, 14, 14, 13, 13, 13, 13, 13, 13
    };

    if (perr != nullptr) {
        *perr = 0;
    }

    const char* sc = s;
    while (*sc == ' ' || (*sc >= 0x09 && *sc <= 0x0D)) {
        ++sc;
    }

    char sign = (*sc == '-' || *sc == '+') ? *sc++ : '+';
    if (base < 0 || base == 1 || _Base_max < base) {
        if (endptr != nullptr) {
            *endptr = const_cast<char*>(s);
        }
        return 0;
    }

    if (0 < base) {
        if (base == 16 && *sc == '0' && (sc[1] == 'x' || sc[1] == 'X')) {
            sc += 2;
        }
    } else if (*sc != '0') {
        base = 10;
    } else if (sc[1] == 'x' || sc[1] == 'X') {
        base = 16;
        sc += 2;
    } else {
        base = 8;
    }

    const char* s1 = sc;
    while (*sc == '0') {
        ++sc;
    }

    unsigned long long x = 0;
    unsigned long long y = 0;
    char dig = 0;
    const char* s2 = sc;
    const char* sd = nullptr;

    for (; (sd = static_cast<const char*>(memchr(&digits[0], (sc[0] >= 'A' && sc[0] <= 'Z') ? (sc[0] + ('a' - 'A')) : sc[0], base))) != nullptr;
         ++sc) {
        y = x;
        dig = static_cast<char>(sd - digits);
        x = x * base + dig;
    }

    if (s1 == sc) {
        if (endptr != nullptr) {
            *endptr = const_cast<char*>(s);
        }
        return 0;
    }

    ptrdiff_t n = sc - s2 - ndigs[base];
    if (n >= 0 && (0 < n || x < x - dig || (x - dig) / base != y)) {
        errno = 34; // ERANGE
        if (perr != nullptr) {
            *perr = 1;
        }
        x = 0xFFFFFFFFFFFFFFFFULL;
        sign = '+';
    }

    if (sign == '-') {
        x = 0 - x;
    }

    if (endptr != nullptr) {
        *endptr = const_cast<char*>(sc);
    }

    return x;
}

unsigned long long __cdecl _Stoull(const char* s, char** endptr, int base) noexcept {
    return _Stoullx(s, endptr, base, nullptr);
}

long long __cdecl _Stollx(const char* s, char** endptr, int base, int* perr) noexcept {
    char* se = nullptr;
    if (endptr == nullptr) {
        endptr = &se;
    }

    const char* sc = s;
    while (*sc == ' ' || (*sc >= 0x09 && *sc <= 0x0D)) {
        ++sc;
    }

    char sign = (*sc == '-' || *sc == '+') ? *sc++ : '+';
    unsigned long long x = _Stoullx(sc, endptr, base, perr);
    if (sc == *endptr) {
        *endptr = const_cast<char*>(s);
    }

    if ((s == *endptr && x != 0) || (sign == '+' && 0x7FFFFFFFFFFFFFFFULL < x) ||
        (sign == '-' && (1ULL << 63) < x)) {
        errno = 34; // ERANGE
        if (perr != nullptr) {
            *perr = 1;
        }
        return sign == '-' ? (-9223372036854775807LL - 1LL) : 9223372036854775807LL;
    }

    return static_cast<long long>(sign == '-' ? 0 - x : x);
}

long long __cdecl _Stoll(const char* s, char** endptr, int base) noexcept {
    return _Stollx(s, endptr, base, nullptr);
}

unsigned long __cdecl _Stoulx(const char* s, char** endptr, int base, int* perr) noexcept {
    constexpr int _Base_max = 36;
    static constexpr char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    static constexpr char ndigs[_Base_max + 1] = {
        0, 0, 33, 21, 17, 14, 13, 12, 11, 11, 10, 10, 9, 9, 9, 9, 9, 8, 8, 8,
        8, 8, 8, 8, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
    };

    if (perr != nullptr) {
        *perr = 0;
    }

    const char* sc = s;
    while (*sc == ' ' || (*sc >= 0x09 && *sc <= 0x0D)) {
        ++sc;
    }

    char sign = (*sc == '-' || *sc == '+') ? *sc++ : '+';
    if (base < 0 || base == 1 || _Base_max < base) {
        if (endptr != nullptr) {
            *endptr = const_cast<char*>(s);
        }
        return 0;
    }

    if (0 < base) {
        if (base == 16 && *sc == '0' && (sc[1] == 'x' || sc[1] == 'X')) {
            sc += 2;
        }
    } else if (*sc != '0') {
        base = 10;
    } else if (sc[1] == 'x' || sc[1] == 'X') {
        base = 16;
        sc += 2;
    } else {
        base = 8;
    }

    const char* s1 = sc;
    while (*sc == '0') {
        ++sc;
    }

    unsigned long x = 0;
    unsigned long y = 0;
    char dig = 0;
    const char* s2 = sc;
    const char* sd = nullptr;

    for (; (sd = static_cast<const char*>(memchr(&digits[0], (sc[0] >= 'A' && sc[0] <= 'Z') ? (sc[0] + ('a' - 'A')) : sc[0], base))) != nullptr;
         ++sc) {
        y = x;
        dig = static_cast<char>(sd - digits);
        x = x * base + dig;
    }

    if (s1 == sc) {
        if (endptr != nullptr) {
            *endptr = const_cast<char*>(s);
        }
        return 0;
    }

    ptrdiff_t n = sc - s2 - ndigs[base];
    if (n >= 0 && (0 < n || x < x - dig || (x - dig) / base != y)) {
        errno = 34; // ERANGE
        if (perr != nullptr) {
            *perr = 1;
        }
        x = 0xFFFFFFFFUL;
        sign = '+';
    }

    if (sign == '-') {
        x = 0 - x;
    }

    if (endptr != nullptr) {
        *endptr = const_cast<char*>(sc);
    }

    return x;
}

unsigned long __cdecl _Stoul(const char* s, char** endptr, int base) noexcept {
    return _Stoulx(s, endptr, base, nullptr);
}

long __cdecl _Stolx(const char* s, char** endptr, int base, int* perr) noexcept {
    char* se = nullptr;
    if (endptr == nullptr) {
        endptr = &se;
    }

    const char* sc = s;
    while (*sc == ' ' || (*sc >= 0x09 && *sc <= 0x0D)) {
        ++sc;
    }

    char sign = (*sc == '-' || *sc == '+') ? *sc++ : '+';
    unsigned long x = _Stoulx(sc, endptr, base, perr);
    if (sc == *endptr) {
        *endptr = const_cast<char*>(s);
    }

    if ((s == *endptr && x != 0) || (sign == '+' && 0x7FFFFFFFL < x) ||
        (sign == '-' && (1UL << 31) < x)) {
        errno = 34; // ERANGE
        if (perr != nullptr) {
            *perr = 1;
        }
        return sign == '-' ? (-2147483647L - 1L) : 2147483647L;
    }

    return static_cast<long>(sign == '-' ? 0 - x : x);
}

long __cdecl _Stol(const char* s, char** endptr, int base) noexcept {
    return _Stolx(s, endptr, base, nullptr);
}

float __cdecl _Stofx(const char* s, char** endptr, long pten, int* perr) noexcept {
    int orig = errno;
    errno = 0;
    float val = static_cast<float>(strtod(s, endptr));
    if (perr != nullptr) {
        *perr = errno;
    }
    errno = orig;
    if (pten != 0) {
        double factor = 1.0;
        long p = pten < 0 ? -pten : pten;
        for (long i = 0; i < p; ++i) factor *= 10.0;
        if (pten < 0) val = static_cast<float>(val / factor);
        else val = static_cast<float>(val * factor);
    }
    return val;
}

float __cdecl _Stof(const char* s, char** endptr, long pten) noexcept {
    return _Stofx(s, endptr, pten, nullptr);
}

double __cdecl _Stodx(const char* s, char** endptr, long pten, int* perr) noexcept {
    int orig = errno;
    errno = 0;
    double val = strtod(s, endptr);
    if (perr != nullptr) {
        *perr = errno;
    }
    errno = orig;
    if (pten != 0) {
        double factor = 1.0;
        long p = pten < 0 ? -pten : pten;
        for (long i = 0; i < p; ++i) factor *= 10.0;
        if (pten < 0) val = val / factor;
        else val = val * factor;
    }
    return val;
}

double __cdecl _Stod(const char* s, char** endptr, long pten) noexcept {
    return _Stodx(s, endptr, pten, nullptr);
}

long double __cdecl _Stoldx(const char* s, char** endptr, long pten, int* perr) noexcept {
    return static_cast<long double>(_Stodx(s, endptr, pten, perr));
}

long double __cdecl _Stold(const char* s, char** endptr, long pten) noexcept {
    return _Stodx(s, endptr, pten, nullptr);
}

// Vector trivial reversing & copy implementations for MSVC STL
void __cdecl __std_reverse_trivially_swappable_1(void* _First, void* _Last) noexcept {
    auto first = static_cast<unsigned char*>(_First);
    auto last = static_cast<unsigned char*>(_Last);
    while (first < last) {
        --last;
        unsigned char t = *first;
        *first = *last;
        *last = t;
        ++first;
    }
}

void __cdecl __std_reverse_trivially_swappable_2(void* _First, void* _Last) noexcept {
    auto first = static_cast<unsigned short*>(_First);
    auto last = static_cast<unsigned short*>(_Last);
    while (first < last) {
        --last;
        unsigned short t = *first;
        *first = *last;
        *last = t;
        ++first;
    }
}

void __cdecl __std_reverse_trivially_swappable_4(void* _First, void* _Last) noexcept {
    auto first = static_cast<unsigned int*>(_First);
    auto last = static_cast<unsigned int*>(_Last);
    while (first < last) {
        --last;
        unsigned int t = *first;
        *first = *last;
        *last = t;
        ++first;
    }
}

void __cdecl __std_reverse_trivially_swappable_8(void* _First, void* _Last) noexcept {
    auto first = static_cast<unsigned long long*>(_First);
    auto last = static_cast<unsigned long long*>(_Last);
    while (first < last) {
        --last;
        unsigned long long t = *first;
        *first = *last;
        *last = t;
        ++first;
    }
}

void __cdecl __std_reverse_copy_trivially_copyable_1(const void* _First, const void* _Last, void* _Dest) noexcept {
    auto first = static_cast<const unsigned char*>(_First);
    auto last = static_cast<const unsigned char*>(_Last);
    auto dest = static_cast<unsigned char*>(_Dest);
    while (last > first) {
        *dest++ = *--last;
    }
}

void __cdecl __std_reverse_copy_trivially_copyable_2(const void* _First, const void* _Last, void* _Dest) noexcept {
    auto first = static_cast<const unsigned short*>(_First);
    auto last = static_cast<const unsigned short*>(_Last);
    auto dest = static_cast<unsigned short*>(_Dest);
    while (last > first) {
        *dest++ = *--last;
    }
}

void __cdecl __std_reverse_copy_trivially_copyable_4(const void* _First, const void* _Last, void* _Dest) noexcept {
    auto first = static_cast<const unsigned int*>(_First);
    auto last = static_cast<const unsigned int*>(_Last);
    auto dest = static_cast<unsigned int*>(_Dest);
    while (last > first) {
        *dest++ = *--last;
    }
}

void __cdecl __std_reverse_copy_trivially_copyable_8(const void* _First, const void* _Last, void* _Dest) noexcept {
    auto first = static_cast<const unsigned long long*>(_First);
    auto last = static_cast<const unsigned long long*>(_Last);
    auto dest = static_cast<unsigned long long*>(_Dest);
    while (last > first) {
        *dest++ = *--last;
    }
}

} // extern "C"
