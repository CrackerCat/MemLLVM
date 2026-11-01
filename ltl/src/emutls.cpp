// ============================================================================
// MemLLVM-LTL Native Emulated TLS Runtime (thread_local in Flat Binaries)
// Allocates Win32 TLS slots dynamically for zero-dependency flat machine code
// ============================================================================

#include "ltl_config.h"
#include <windows.h>
#include <cstdint>
#include <cstring>

#pragma bss_seg(".data")
#pragma data_seg(".data")

static volatile DWORD g_emutls_slot = TLS_OUT_OF_INDEXES;
static volatile uintptr_t g_emutls_num_vars = 0;

struct __emutls_thread_array {
    size_t capacity;
    void* data[1];
};

extern "C" void* MemLLVM_GetThreadLocalAddress(
    volatile uint32_t* pIndex,
    size_t size,
    const void* templ) {
    if (g_emutls_slot == TLS_OUT_OF_INDEXES) {
        DWORD slot = TlsAlloc();
        if (InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(&g_emutls_slot), slot, TLS_OUT_OF_INDEXES) != TLS_OUT_OF_INDEXES) {
            TlsFree(slot);
        }
    }

    uint32_t index = *pIndex;
    if (index == 0) {
        index = static_cast<uint32_t>(InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_emutls_num_vars)));
        *pIndex = index;
    }

    HANDLE hHeap = GetProcessHeap();
    auto* arr = static_cast<__emutls_thread_array*>(TlsGetValue(g_emutls_slot));
    if (!arr || index > arr->capacity) {
        size_t new_cap = index + 32;
        size_t new_size = sizeof(__emutls_thread_array) + new_cap * sizeof(void*);
        __emutls_thread_array* new_arr = nullptr;
        if (!arr) {
            new_arr = static_cast<__emutls_thread_array*>(HeapAlloc(hHeap, HEAP_ZERO_MEMORY, new_size));
            if (new_arr) new_arr->capacity = new_cap;
        } else {
            new_arr = static_cast<__emutls_thread_array*>(HeapReAlloc(hHeap, HEAP_ZERO_MEMORY, arr, new_size));
            if (new_arr) new_arr->capacity = new_cap;
        }
        if (new_arr) {
            TlsSetValue(g_emutls_slot, new_arr);
            arr = new_arr;
        }
    }

    if (!arr) return nullptr;

    void* ptr = arr->data[index - 1];
    if (!ptr) {
        ptr = HeapAlloc(hHeap, HEAP_ZERO_MEMORY, size);
        if (ptr && templ) {
            memcpy(ptr, templ, size);
        }
        arr->data[index - 1] = ptr;
    }
    return ptr;
}
