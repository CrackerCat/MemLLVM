// ============================================================================
// MemLLVM-LTL Parallel Algorithms Runtime (C++17 <execution> for Flat Code)
// Bridges std::execution::par directly to Win32 Threadpool APIs
// ============================================================================

#include "ltl_config.h"
#include <windows.h>
#include <cstdint>
#include <cstddef>

#pragma bss_seg(".data")
#pragma data_seg(".data")

extern "C" {

unsigned int __stdcall __std_parallel_algorithms_hw_threads() noexcept {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors ? si.dwNumberOfProcessors : 1;
}

PTP_WORK __stdcall __std_create_threadpool_work(
    PTP_WORK_CALLBACK callback, void* context, PTP_CALLBACK_ENVIRON env) noexcept {
    return CreateThreadpoolWork(callback, context, env);
}

void __stdcall __std_submit_threadpool_work(PTP_WORK work) noexcept {
    SubmitThreadpoolWork(work);
}

void __stdcall __std_bulk_submit_threadpool_work(PTP_WORK work, size_t submissions) noexcept {
    for (size_t i = 0; i < submissions; ++i) {
        SubmitThreadpoolWork(work);
    }
}

void __stdcall __std_close_threadpool_work(PTP_WORK work) noexcept {
    CloseThreadpoolWork(work);
}

void __stdcall __std_wait_for_threadpool_work_callbacks(PTP_WORK work, BOOL cancel) noexcept {
    WaitForThreadpoolWorkCallbacks(work, cancel);
}

void __stdcall __std_execution_wait_on_uchar(const volatile unsigned char*, unsigned char) noexcept {}
void __stdcall __std_execution_wake_by_address_all(const volatile void*) noexcept {}

} // extern "C"

namespace std {
    struct nothrow_t { explicit nothrow_t() = default; };
    extern const nothrow_t nothrow{};
    enum class align_val_t : size_t {};
}

void* __cdecl operator new(size_t size, std::align_val_t, const std::nothrow_t&) noexcept {
    return HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
}

void __cdecl operator delete(void* ptr, size_t, std::align_val_t) noexcept {
    if (ptr) HeapFree(GetProcessHeap(), 0, ptr);
}
