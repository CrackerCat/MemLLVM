// ============================================================================
// MemLLVM-LTL CRT Lifecycle & Process Management (atexit, _doexit, exit)
// Split into independent translation unit so modules without atexit/dtors
// do not link s_memllvm_atexit_table and avoid creating a .data section.
// ============================================================================

#include <cstddef>
#include <cstdint>

#pragma comment(linker, "/merge:.rdata=.text")

extern "C" void __stdcall ExitProcess(unsigned int uExitCode);

extern "C" {

unsigned long __stdcall GetActiveProcessorCount(unsigned short GroupNumber) noexcept {
    return 1;
}
extern "C" void* const __imp_GetActiveProcessorCount = (void*)GetActiveProcessorCount;

void* __stdcall GetProcessHeap();
void* __stdcall HeapAlloc(void* hHeap, unsigned int dwFlags, size_t dwBytes);
void* __stdcall HeapReAlloc(void* hHeap, unsigned int dwFlags, void* lpMem, size_t dwBytes);
int __stdcall HeapFree(void* hHeap, unsigned int dwFlags, void* lpMem);

typedef void (__cdecl *atexit_func_t)();
typedef void (__cdecl *cxa_dtor_t)(void *);
typedef void (__cdecl *_PVFV)(void);

struct MemLLVMAtexitEntry {
    cxa_dtor_t func;
    void *arg;
    bool is_cxa;
};

static MemLLVMAtexitEntry* s_memllvm_atexit_table = nullptr;
static size_t s_memllvm_atexit_capacity = 0;
static size_t s_memllvm_atexit_count = 0;

static bool ensure_atexit_capacity() {
    if (s_memllvm_atexit_count < s_memllvm_atexit_capacity) return true;
    size_t new_cap = (s_memllvm_atexit_capacity == 0) ? 32 : s_memllvm_atexit_capacity * 2;
    void* heap = GetProcessHeap();
    if (!heap) return false;
    void* new_buf = s_memllvm_atexit_table
        ? HeapReAlloc(heap, 0, s_memllvm_atexit_table, new_cap * sizeof(MemLLVMAtexitEntry))
        : HeapAlloc(heap, 0, new_cap * sizeof(MemLLVMAtexitEntry));
    if (!new_buf) return false;
    s_memllvm_atexit_table = static_cast<MemLLVMAtexitEntry*>(new_buf);
    s_memllvm_atexit_capacity = new_cap;
    return true;
}

int atexit(void (__cdecl *func)()) {
    if (!func) return -1;
    if (!ensure_atexit_capacity()) return -1;
    s_memllvm_atexit_table[s_memllvm_atexit_count++] = { reinterpret_cast<cxa_dtor_t>(func), nullptr, false };
    return 0;
}

int __cxa_atexit(void (__cdecl *func)(void *), void *arg, void *dso_handle) {
    (void)dso_handle;
    if (!func) return -1;
    if (!ensure_atexit_capacity()) return -1;
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
    if (s_memllvm_atexit_table) {
        void* heap = GetProcessHeap();
        if (heap) HeapFree(heap, 0, s_memllvm_atexit_table);
        s_memllvm_atexit_table = nullptr;
        s_memllvm_atexit_capacity = 0;
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

} // extern "C"
