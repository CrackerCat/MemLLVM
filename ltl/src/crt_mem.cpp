// ============================================================================
// MemLLVM-LTL CRT Memory Intrinsics (memcpy, memmove, memset)
// Fallback for LLVM CodeGen lowering in freestanding environments
// ============================================================================

#include <cstddef>
#include <cstdint>

// Embed linker directives into .drectve to automatically merge .rdata into .text
#pragma comment(linker, "/merge:.rdata=.text")

extern "C" void __stdcall ExitProcess(unsigned int uExitCode);

extern "C" {

typedef void (*__memllvm_init_fn_t)(void);

#pragma section(".rdata$mlia", read)
#pragma section(".rdata$mliz", read)

__declspec(allocate(".rdata$mlia")) const __memllvm_init_fn_t __memllvm_mli_start = nullptr;
__declspec(allocate(".rdata$mliz")) const __memllvm_init_fn_t __memllvm_mli_end = nullptr;

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
    __builtin_unreachable();
}

#pragma function(memcpy)
void* memcpy(void* dest, const void* src, size_t count) {
    void* d = dest;
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(src), "+c"(count) :: "memory");
    return dest;
}

#pragma function(memmove)
void* memmove(void* dest, const void* src, size_t count) {
    void* d = dest;
    if (dest <= src || (const char*)dest >= (const char*)src + count) {
        __asm__ volatile("rep movsb" : "+D"(d), "+S"(src), "+c"(count) :: "memory");
    } else {
        d = (char*)dest + count - 1;
        src = (const char*)src + count - 1;
        __asm__ volatile("std; rep movsb; cld" : "+D"(d), "+S"(src), "+c"(count) :: "memory");
    }
    return dest;
}

#pragma function(memset)
void* memset(void* dest, int c, size_t count) {
    void* d = dest;
    __asm__ volatile("rep stosb" : "+D"(d), "+c"(count) : "a"((unsigned char)c) : "memory");
    return dest;
}

// clang has a LibFunc builtin for memcmp, so without this pragma the loop body
// gets optimised straight back into a recursive call to memcmp.
#pragma function(memcmp)
int memcmp(const void* s1, const void* s2, size_t n) {
    if (s1 == s2 || !n) return 0;
    int res = 0;
    __asm__ volatile(
        "xorl %%eax, %%eax\n\t"
        "repe cmpsb\n\t"
        "je 1f\n\t"
        "movzbl -1(%%rsi), %%eax\n\t"
        "movzbl -1(%%rdi), %%edx\n\t"
        "subl %%edx, %%eax\n\t"
        "1:\n\t"
        : "=a"(res), "+S"(s1), "+D"(s2), "+c"(n)
        :: "edx", "cc"
    );
    return res;
}

#pragma function(strlen)
size_t strlen(const char* s) {
    if (!s) return 0;
    size_t len;
    __asm__ volatile(
        "xorl %%eax, %%eax\n\t"
        "movq $-1, %%rcx\n\t"
        "repne scasb\n\t"
        "notq %%rcx\n\t"
        "decq %%rcx\n\t"
        : "=c"(len), "+D"(s)
        :
        : "al", "cc"
    );
    return len;
}

#pragma function(wcslen)
size_t wcslen(const wchar_t* s) {
    if (!s) return 0;
    size_t len;
    __asm__ volatile(
        "xorl %%eax, %%eax\n\t"
        "movq $-1, %%rcx\n\t"
        "repne scasw\n\t"
        "notq %%rcx\n\t"
        "decq %%rcx\n\t"
        : "=c"(len), "+D"(s)
        :
        : "ax", "cc"
    );
    return len;
}

void __cdecl _wassert(const wchar_t* msg, const wchar_t* file, unsigned line) {
    (void)msg; (void)file; (void)line;
    ExitProcess(0xc0000420);
}

void __cdecl _assert(const char* msg, const char* file, unsigned line) {
    (void)msg; (void)file; (void)line;
    ExitProcess(0xc0000420);
}

char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}

char* strncpy(char* dest, const char* src, size_t n) {
    char* d = dest;
    while (n && (*d++ = *src++)) --n;
    while (n--) *d++ = '\0';
    return dest;
}

unsigned long __stdcall GetCurrentThreadId() {
    unsigned long tid;
    __asm__ volatile("movl %%gs:0x48, %0" : "=r"(tid));
    return tid;
}

unsigned long __stdcall GetCurrentProcessId() {
    unsigned long pid;
    __asm__ volatile("movl %%gs:0x40, %0" : "=r"(pid));
    return pid;
}

void* __stdcall GetProcessHeap() {
    void* heap;
    __asm__ volatile(
        "movq %%gs:0x60, %0\n\t"
        "movq 0x30(%0), %0"
        : "=r"(heap)
    );
    return heap;
}

void* __stdcall GetStdHandle(unsigned long nStdHandle) {
    uint8_t* peb;
    __asm__ volatile("movq %%gs:0x60, %0" : "=r"(peb));
    if (!peb) return (void*)-1;
    uint8_t* params = *reinterpret_cast<uint8_t**>(peb + 0x20);
    if (!params) return (void*)-1;
    if (nStdHandle == static_cast<unsigned long>(-10)) return *reinterpret_cast<void**>(params + 0x20);
    if (nStdHandle == static_cast<unsigned long>(-11)) return *reinterpret_cast<void**>(params + 0x28);
    if (nStdHandle == static_cast<unsigned long>(-12)) return *reinterpret_cast<void**>(params + 0x30);
    return (void*)-1;
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
    size_t count = static_cast<const uint16_t*>(_Last) - static_cast<const uint16_t*>(_First);
    if (!count) return _Last;
    const void* res = _First;
    __asm__ volatile(
        "repne scasw\n\t"
        "jnz 1f\n\t"
        "subq $2, %%rdi\n\t"
        "movq %%rdi, %0\n\t"
        "jmp 2f\n\t"
        "1:\n\t"
        "movq %1, %0\n\t"
        "2:\n\t"
        : "=r"(res)
        : "r"(_Last), "0"(res), "a"(_Val), "c"(count), "D"(res)
        : "cc"
    );
    return res;
}

const void* __stdcall __std_find_trivial_4(const void* _First, const void* _Last, uint32_t _Val) noexcept {
    size_t count = static_cast<const uint32_t*>(_Last) - static_cast<const uint32_t*>(_First);
    if (!count) return _Last;
    const void* res = _First;
    __asm__ volatile(
        "repne scasl\n\t"
        "jnz 1f\n\t"
        "subq $4, %%rdi\n\t"
        "movq %%rdi, %0\n\t"
        "jmp 2f\n\t"
        "1:\n\t"
        "movq %1, %0\n\t"
        "2:\n\t"
        : "=r"(res)
        : "r"(_Last), "0"(res), "a"(_Val), "c"(count), "D"(res)
        : "cc"
    );
    return res;
}

const void* __stdcall __std_find_trivial_8(const void* _First, const void* _Last, uint64_t _Val) noexcept {
    size_t count = static_cast<const uint64_t*>(_Last) - static_cast<const uint64_t*>(_First);
    if (!count) return _Last;
    const void* res = _First;
    __asm__ volatile(
        "repne scasq\n\t"
        "jnz 1f\n\t"
        "subq $8, %%rdi\n\t"
        "movq %%rdi, %0\n\t"
        "jmp 2f\n\t"
        "1:\n\t"
        "movq %1, %0\n\t"
        "2:\n\t"
        : "=r"(res)
        : "r"(_Last), "0"(res), "a"(_Val), "c"(count), "D"(res)
        : "cc"
    );
    return res;
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

} // extern "C"

namespace std {
const char* __cdecl _Syserror_map(int) {
    return "system error";
}
} // namespace std
