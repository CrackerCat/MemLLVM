// ============================================================================
// MemLLVM-LTL CRT Memory Intrinsics (memcpy, memmove, memset)
// Fallback for LLVM CodeGen lowering in freestanding environments
// ============================================================================

#include <cstddef>
#include <cstdint>

extern "C" void __stdcall ExitProcess(unsigned int uExitCode);

extern "C" {

typedef void (*__memllvm_init_fn_t)(void);

#pragma section(".mli$a", read)
#pragma section(".mli$z", read)

__declspec(allocate(".mli$a")) const __memllvm_init_fn_t __memllvm_mli_start = nullptr;
__declspec(allocate(".mli$z")) const __memllvm_init_fn_t __memllvm_mli_end = nullptr;

void __memllvm_dispatch_tu_inits(void) {
    const __memllvm_init_fn_t* cur = &__memllvm_mli_start + 1;
    const __memllvm_init_fn_t* end = &__memllvm_mli_end;
    for (; cur < end; ++cur) {
        if (*cur) {
            (*cur)();
        }
    }
}

__declspec(selectany) int _fltused = 0x9875;

int __CxxFrameHandler3() {
    return 0;
}

// SEH personality routine for functions containing __try/__except.
//
// clang-cl lowers `__try` to a `personality ptr @__C_specific_handler` operand
// on the enclosing function, so this symbol is referenced by the object file
// even though nothing ever *calls* it from generated code. A flat binary has no
// registered .pdata/UNWIND_INFO, so the OS will never dispatch through it
// either; this definition exists so the link succeeds.
//
// Return value is an EXCEPTION_DISPOSITION. 1 == ExceptionContinueSearch is the
// safe "I do not handle this" answer, which also means: a hardware fault inside
// a __try block in a MemLLVM image is NOT caught by the __except clause.
int __C_specific_handler(void* ExceptionRecord, void* EstablisherFrame,
                         void* ContextRecord, void* DispatcherContext) {
    (void)ExceptionRecord;
    (void)EstablisherFrame;
    (void)ContextRecord;
    (void)DispatcherContext;
    return 1; // ExceptionContinueSearch
}

[[noreturn]] void __cdecl __std_terminate() noexcept {
    ExitProcess(0xc0000025);
}

#pragma function(memcpy)
void* memcpy(void* dest, const void* src, size_t count) {
    char* d = static_cast<char*>(dest);
    const char* s = static_cast<const char*>(src);
    for (size_t i = 0; i < count; ++i) {
        d[i] = s[i];
    }
    return dest;
}

#pragma function(memmove)
void* memmove(void* dest, const void* src, size_t count) {
    char* d = static_cast<char*>(dest);
    const char* s = static_cast<const char*>(src);
    if (d < s) {
        for (size_t i = 0; i < count; ++i) {
            d[i] = s[i];
        }
    } else if (d > s) {
        for (size_t i = count; i > 0; --i) {
            d[i - 1] = s[i - 1];
        }
    }
    return dest;
}

#pragma function(memset)
void* memset(void* dest, int c, size_t count) {
    char* d = static_cast<char*>(dest);
    for (size_t i = 0; i < count; ++i) {
        d[i] = static_cast<char>(c);
    }
    return dest;
}

// clang has a LibFunc builtin for memcmp, so without this pragma the loop body
// gets optimised straight back into a recursive call to memcmp.
#pragma function(memcmp)
int memcmp(const void* s1, const void* s2, size_t n) {
    const unsigned char* p1 = static_cast<const unsigned char*>(s1);
    const unsigned char* p2 = static_cast<const unsigned char*>(s2);
    for (size_t i = 0; i < n; ++i) {
        if (p1[i] != p2[i]) return (int)p1[i] - (int)p2[i];
    }
    return 0;
}

float ceilf(float x) {
    float res;
    __asm__("roundss $2, %1, %0" : "=x"(res) : "x"(x));
    return res;
}

float floorf(float x) {
    float res;
    __asm__("roundss $1, %1, %0" : "=x"(res) : "x"(x));
    return res;
}

double ceil(double x) {
    double res;
    __asm__("roundsd $2, %1, %0" : "=x"(res) : "x"(x));
    return res;
}

double floor(double x) {
    double res;
    __asm__("roundsd $1, %1, %0" : "=x"(res) : "x"(x));
    return res;
}

double round(double x) {
    double res;
    __asm__("roundsd $0, %1, %0" : "=x"(res) : "x"(x));
    return res;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return (unsigned char)s1[i] - (unsigned char)s2[i];
        if (!s1[i]) break;
    }
    return 0;
}

static inline char tolower_ascii(char c) {
    return (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
}

static inline wchar_t towlower_ascii(wchar_t c) {
    return (c >= L'A' && c <= L'Z') ? (c + (L'a' - L'A')) : c;
}

int _stricmp(const char* s1, const char* s2) {
    while (*s1 && (tolower_ascii(*s1) == tolower_ascii(*s2))) {
        s1++;
        s2++;
    }
    return (unsigned char)tolower_ascii(*s1) - (unsigned char)tolower_ascii(*s2);
}

int _strnicmp(const char* s1, const char* s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char c1 = tolower_ascii(s1[i]);
        char c2 = tolower_ascii(s2[i]);
        if (c1 != c2) return (unsigned char)c1 - (unsigned char)c2;
        if (!c1) break;
    }
    return 0;
}

int wcscmp(const wchar_t* s1, const wchar_t* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return (int)(*s1 - *s2);
}

int wcsncmp(const wchar_t* s1, const wchar_t* s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return (int)(s1[i] - s2[i]);
        if (!s1[i]) break;
    }
    return 0;
}

#pragma function(wcschr)
wchar_t* wcschr(const wchar_t* s, wchar_t c) {
    if (!s) return nullptr;
    while (*s) {
        if (*s == c) return const_cast<wchar_t*>(s);
        ++s;
    }
    return (c == 0) ? const_cast<wchar_t*>(s) : nullptr;
}

#pragma function(wcsstr)
wchar_t* wcsstr(const wchar_t* haystack, const wchar_t* needle) {
    if (!haystack || !needle) return nullptr;
    if (!*needle) return const_cast<wchar_t*>(haystack);
    for (; *haystack; ++haystack) {
        const wchar_t* h = haystack;
        const wchar_t* n = needle;
        while (*h && *n && *h == *n) { ++h; ++n; }
        if (!*n) return const_cast<wchar_t*>(haystack);
    }
    return nullptr;
}

int _wcsicmp(const wchar_t* s1, const wchar_t* s2) {
    while (*s1 && (towlower_ascii(*s1) == towlower_ascii(*s2))) {
        s1++;
        s2++;
    }
    return (int)(towlower_ascii(*s1) - towlower_ascii(*s2));
}

int _wcsnicmp(const wchar_t* s1, const wchar_t* s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        wchar_t c1 = towlower_ascii(s1[i]);
        wchar_t c2 = towlower_ascii(s2[i]);
        if (c1 != c2) return (int)(c1 - c2);
        if (!c1) break;
    }
    return 0;
}

int _memicmp(const void* s1, const void* s2, size_t n) {
    const unsigned char* p1 = static_cast<const unsigned char*>(s1);
    const unsigned char* p2 = static_cast<const unsigned char*>(s2);
    for (size_t i = 0; i < n; i++) {
        unsigned char c1 = tolower_ascii(p1[i]);
        unsigned char c2 = tolower_ascii(p2[i]);
        if (c1 != c2) return (int)c1 - (int)c2;
    }
    return 0;
}

int __ascii_memicmp(const void* s1, const void* s2, size_t n) { return _memicmp(s1, s2, n); }
int __ascii_stricmp(const char* s1, const char* s2) { return _stricmp(s1, s2); }
int __ascii_strnicmp(const char* s1, const char* s2, size_t n) { return _strnicmp(s1, s2, n); }
int __ascii_wcsicmp(const wchar_t* s1, const wchar_t* s2) { return _wcsicmp(s1, s2); }
int __ascii_wcsnicmp(const wchar_t* s1, const wchar_t* s2, size_t n) { return _wcsnicmp(s1, s2, n); }

static void swap_bytes(char* a, char* b, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        char tmp = a[i];
        a[i] = b[i];
        b[i] = tmp;
    }
}

void qsort(void* base, size_t num, size_t size, int (*compar)(const void*, const void*)) {
    if (num <= 1 || size == 0) return;
    char* b = static_cast<char*>(base);
    if (num <= 16) {
        for (size_t i = 1; i < num; ++i) {
            for (size_t j = i; j > 0 && compar(b + (j - 1) * size, b + j * size) > 0; --j) {
                swap_bytes(b + (j - 1) * size, b + j * size, size);
            }
        }
        return;
    }
    char* pivot = b + (num / 2) * size;
    swap_bytes(pivot, b + (num - 1) * size, size);
    char* pval = b + (num - 1) * size;
    size_t i = 0;
    for (size_t j = 0; j < num - 1; ++j) {
        if (compar(b + j * size, pval) <= 0) {
            if (i != j) {
                swap_bytes(b + i * size, b + j * size, size);
            }
            ++i;
        }
    }
    swap_bytes(b + i * size, b + (num - 1) * size, size);
    qsort(b, i, size, compar);
    qsort(b + (i + 1) * size, num - i - 1, size, compar);
}

void* bsearch(const void* key, const void* base, size_t num, size_t size, int (*compar)(const void*, const void*)) {
    const char* b = static_cast<const char*>(base);
    size_t l = 0, r = num;
    while (l < r) {
        size_t m = l + (r - l) / 2;
        int cmp = compar(key, b + m * size);
        if (cmp == 0) return const_cast<void*>(static_cast<const void*>(b + m * size));
        if (cmp < 0) r = m;
        else l = m + 1;
    }
    return nullptr;
}

// Windows x64 __chkstk & _alloca_probe
// When stack frame allocation exceeds 4096 bytes, clang-cl passes allocation size
// in RAX and invokes __chkstk. It probes downward page-by-page from StackLimit
// (gs:[0x10]) to trip OS guard page allocation without skipping pages.
// Preserves RAX, RCX, RDX, R8, R9, R10, R11 and restores caller RSP.
__asm__(
    ".intel_syntax noprefix\n"
    ".globl __chkstk\n"
    ".globl _alloca_probe\n"
    "__chkstk:\n"
    "_alloca_probe:\n"
    "    sub rsp, 16\n"
    "    mov [rsp], r10\n"
    "    mov [rsp+8], r11\n"
    "    xor r11, r11\n"
    "    lea r10, [rsp+24]\n"
    "    sub r10, rax\n"
    "    cmovb r10, r11\n"
    "    mov r11, qword ptr gs:[16]\n"
    "    cmp r10, r11\n"
    "    jae .Lcs20\n"
    "    and r10w, 0xf000\n"
    ".Lcs10:\n"
    "    lea r11, [r11-4096]\n"
    "    mov byte ptr [r11], 0\n"
    "    cmp r10, r11\n"
    "    jne .Lcs10\n"
    ".Lcs20:\n"
    "    mov r10, [rsp]\n"
    "    mov r11, [rsp+8]\n"
    "    add rsp, 16\n"
    "    ret\n"
);

const void* __stdcall __std_find_trivial_2(const void* _First, const void* _Last, uint16_t _Val) noexcept {
    const uint16_t* p = static_cast<const uint16_t*>(_First);
    const uint16_t* end = static_cast<const uint16_t*>(_Last);
    for (; p != end; ++p) {
        if (*p == _Val) return p;
    }
    return end;
}

const void* __stdcall __std_find_trivial_4(const void* _First, const void* _Last, uint32_t _Val) noexcept {
    const uint32_t* p = static_cast<const uint32_t*>(_First);
    const uint32_t* end = static_cast<const uint32_t*>(_Last);
    for (; p != end; ++p) {
        if (*p == _Val) return p;
    }
    return end;
}

const void* __stdcall __std_find_trivial_8(const void* _First, const void* _Last, uint64_t _Val) noexcept {
    const uint64_t* p = static_cast<const uint64_t*>(_First);
    const uint64_t* end = static_cast<const uint64_t*>(_Last);
    for (; p != end; ++p) {
        if (*p == _Val) return p;
    }
    return end;
}

size_t __stdcall __std_count_trivial_1(const void* _First, const void* _Last, uint8_t _Val) noexcept {
    const uint8_t* p = static_cast<const uint8_t*>(_First);
    const uint8_t* end = static_cast<const uint8_t*>(_Last);
    size_t count = 0;
    for (; p != end; ++p) if (*p == _Val) ++count;
    return count;
}

size_t __stdcall __std_count_trivial_2(const void* _First, const void* _Last, uint16_t _Val) noexcept {
    const uint16_t* p = static_cast<const uint16_t*>(_First);
    const uint16_t* end = static_cast<const uint16_t*>(_Last);
    size_t count = 0;
    for (; p != end; ++p) if (*p == _Val) ++count;
    return count;
}

size_t __stdcall __std_count_trivial_4(const void* _First, const void* _Last, uint32_t _Val) noexcept {
    const uint32_t* p = static_cast<const uint32_t*>(_First);
    const uint32_t* end = static_cast<const uint32_t*>(_Last);
    size_t count = 0;
    for (; p != end; ++p) if (*p == _Val) ++count;
    return count;
}

size_t __stdcall __std_count_trivial_8(const void* _First, const void* _Last, uint64_t _Val) noexcept {
    const uint64_t* p = static_cast<const uint64_t*>(_First);
    const uint64_t* end = static_cast<const uint64_t*>(_Last);
    size_t count = 0;
    for (; p != end; ++p) if (*p == _Val) ++count;
    return count;
}

int32_t __stdcall __std_max_4i(const void* _First, const void* _Last) noexcept {
    const int32_t* p = static_cast<const int32_t*>(_First);
    const int32_t* end = static_cast<const int32_t*>(_Last);
    if (p == end) return 0;
    int32_t max_val = *p++;
    for (; p != end; ++p) {
        if (*p > max_val) max_val = *p;
    }
    return max_val;
}

size_t __stdcall __std_mismatch_1(const void* _First1, const void* _First2, size_t _Count) noexcept {
    const unsigned char* p1 = static_cast<const unsigned char*>(_First1);
    const unsigned char* p2 = static_cast<const unsigned char*>(_First2);
    for (size_t i = 0; i < _Count; ++i) if (p1[i] != p2[i]) return i;
    return _Count;
}

size_t __stdcall __std_mismatch_2(const void* _First1, const void* _First2, size_t _Count) noexcept {
    const unsigned short* p1 = static_cast<const unsigned short*>(_First1);
    const unsigned short* p2 = static_cast<const unsigned short*>(_First2);
    for (size_t i = 0; i < _Count; ++i) if (p1[i] != p2[i]) return i;
    return _Count;
}

size_t __stdcall __std_mismatch_4(const void* _First1, const void* _First2, size_t _Count) noexcept {
    const unsigned int* p1 = static_cast<const unsigned int*>(_First1);
    const unsigned int* p2 = static_cast<const unsigned int*>(_First2);
    for (size_t i = 0; i < _Count; ++i) if (p1[i] != p2[i]) return i;
    return _Count;
}

size_t __stdcall __std_mismatch_8(const void* _First1, const void* _First2, size_t _Count) noexcept {
    const unsigned long long* p1 = static_cast<const unsigned long long*>(_First1);
    const unsigned long long* p2 = static_cast<const unsigned long long*>(_First2);
    for (size_t i = 0; i < _Count; ++i) if (p1[i] != p2[i]) return i;
    return _Count;
}

unsigned long __stdcall GetActiveProcessorCount(unsigned short GroupNumber) noexcept {
    return 1;
}
extern "C" void* const __imp_GetActiveProcessorCount = (void*)GetActiveProcessorCount;

// ============================================================================
// Lifecycle & Process Management (atexit, exit, _initterm, argv reconstruction)
// ============================================================================

void* __stdcall GetCommandLineW();
wchar_t** __stdcall CommandLineToArgvW(const wchar_t* lpCmdLine, int* pNumArgs);
void* __stdcall GetProcessHeap();
void* __stdcall HeapAlloc(void* hHeap, unsigned int dwFlags, size_t dwBytes);
int __stdcall WideCharToMultiByte(unsigned int CodePage, unsigned int dwFlags,
                                  const wchar_t* lpWideCharStr, int cchWideChar,
                                  char* lpMultiByteStr, int cbMultiByte,
                                  const char* lpDefaultChar, int* lpUsedDefaultChar);

typedef void (__cdecl *atexit_func_t)();
typedef void (__cdecl *cxa_dtor_t)(void *);
typedef void (__cdecl *_PVFV)(void);

struct MemLLVMAtexitEntry {
    cxa_dtor_t func;
    void *arg;
    bool is_cxa;
};

static constexpr size_t MEMLLVM_MAX_ATEXIT = 256;
static MemLLVMAtexitEntry s_memllvm_atexit_table[MEMLLVM_MAX_ATEXIT];
static size_t s_memllvm_atexit_count = 0;

int atexit(void (__cdecl *func)()) {
    if (!func) return -1;
    if (s_memllvm_atexit_count >= MEMLLVM_MAX_ATEXIT) return -1;
    s_memllvm_atexit_table[s_memllvm_atexit_count++] = { reinterpret_cast<cxa_dtor_t>(func), nullptr, false };
    return 0;
}

int __cxa_atexit(void (__cdecl *func)(void *), void *arg, void *dso_handle) {
    (void)dso_handle;
    if (!func) return -1;
    if (s_memllvm_atexit_count >= MEMLLVM_MAX_ATEXIT) return -1;
    s_memllvm_atexit_table[s_memllvm_atexit_count++] = { func, arg, true };
    return 0;
}

void _doexit() {
    while (s_memllvm_atexit_count > 0) {
        --s_memllvm_atexit_count;
        auto entry = s_memllvm_atexit_table[s_memllvm_atexit_count];
        if (entry.func) {
            if (entry.is_cxa) {
                entry.func(entry.arg);
            } else {
                reinterpret_cast<atexit_func_t>(entry.func)();
            }
        }
    }
}

void exit(int status) {
    _doexit();
    ExitProcess(static_cast<unsigned int>(status));
}

void _exit(int status) {
    ExitProcess(static_cast<unsigned int>(status));
}

void _initterm(_PVFV *pfbegin, _PVFV *pfend) {
    while (pfbegin < pfend) {
        if (*pfbegin) {
            (**pfbegin)();
        }
        ++pfbegin;
    }
}

int _initterm_e(_PVFV *pfbegin, _PVFV *pfend) {
    while (pfbegin < pfend) {
        if (*pfbegin) {
            (**pfbegin)();
        }
        ++pfbegin;
    }
    return 0;
}

void __memllvm_get_ansi_argv(int *out_argc, char ***out_argv) {
    int numArgs = 0;
    const wchar_t *cmdLine = static_cast<const wchar_t *>(GetCommandLineW());
    wchar_t **argvW = cmdLine ? CommandLineToArgvW(cmdLine, &numArgs) : nullptr;
    if (!argvW || numArgs <= 1) {
        static char fallback_bin[] = "app.bin";
        static char *fallback_argv[] = { fallback_bin, nullptr };
        if (out_argc) *out_argc = 1;
        if (out_argv) *out_argv = fallback_argv;
        return;
    }

    int eff_argc = numArgs - 1;
    void *heap = GetProcessHeap();
    char **argvA = static_cast<char **>(HeapAlloc(heap, 8 /* HEAP_ZERO_MEMORY */, (eff_argc + 1) * sizeof(char *)));
    if (!argvA) {
        static char fallback_bin[] = "app.bin";
        static char *fallback_argv[] = { fallback_bin, nullptr };
        if (out_argc) *out_argc = 1;
        if (out_argv) *out_argv = fallback_argv;
        return;
    }

    for (int i = 0; i < eff_argc; ++i) {
        const wchar_t *wstr = argvW[i + 1];
        int len = WideCharToMultiByte(0 /* CP_ACP */, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            char *buf = static_cast<char *>(HeapAlloc(heap, 0, len));
            if (buf) {
                WideCharToMultiByte(0, 0, wstr, -1, buf, len, nullptr, nullptr);
                argvA[i] = buf;
            } else {
                argvA[i] = const_cast<char *>("");
            }
        } else {
            argvA[i] = const_cast<char *>("");
        }
    }
    argvA[eff_argc] = nullptr;
    if (out_argc) *out_argc = eff_argc;
    if (out_argv) *out_argv = argvA;
}

void __memllvm_get_wide_argv(int *out_argc, wchar_t ***out_argv) {
    int numArgs = 0;
    const wchar_t *cmdLine = static_cast<const wchar_t *>(GetCommandLineW());
    wchar_t **argvW = cmdLine ? CommandLineToArgvW(cmdLine, &numArgs) : nullptr;
    if (!argvW || numArgs <= 1) {
        static wchar_t fallback_bin[] = L"app.bin";
        static wchar_t *fallback_argv[] = { fallback_bin, nullptr };
        if (out_argc) *out_argc = 1;
        if (out_argv) *out_argv = fallback_argv;
        return;
    }
    if (out_argc) *out_argc = numArgs - 1;
    if (out_argv) *out_argv = &argvW[1];
}

} // extern "C"

namespace std {
const char* __cdecl _Syserror_map(int) {
    return "system error";
}
} // namespace std
