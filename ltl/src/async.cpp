// ============================================================================
// MemLLVM-LTL High-Level Concurrency & Async Runtime
// Zero-Bloat std::async, std::future, std::promise and Concurrency PPL runtime
// ============================================================================

#include <windows.h>
#include <cstdint>
#include <functional>
#include <ppltasks.h>
#include <pplwin.h>

#pragma bss_seg(".data")
#pragma data_seg(".data")

extern "C" {

int __stdcall __std_init_once_begin_initialize_clr(
    void** _LpInitOnce, unsigned long _DwFlags, int* _FPending, void** _LpContext) noexcept {
    return InitOnceBeginInitialize((PINIT_ONCE)_LpInitOnce, _DwFlags, (PBOOL)_FPending, _LpContext);
}

int __stdcall __std_init_once_complete_clr(
    void** _LpInitOnce, unsigned long _DwFlags, void* _LpContext) noexcept {
    return InitOnceComplete((PINIT_ONCE)_LpInitOnce, _DwFlags, _LpContext);
}

void __stdcall __std_init_once_link_alternate_names_and_abort() noexcept {
    ExitProcess(1);
}

} // extern "C"

extern "C++" {

void __cdecl __ExceptionPtrCreate(void* ep) noexcept {
    if (ep) *(void**)ep = nullptr;
}

void __cdecl __ExceptionPtrDestroy(void* ep) noexcept {
    if (ep) *(void**)ep = nullptr;
}

void __cdecl __ExceptionPtrCopy(void* dst, const void* src) noexcept {
    if (dst) *(void**)dst = src ? *(void**)src : nullptr;
}

void __cdecl __ExceptionPtrAssign(void* dst, const void* src) noexcept {
    if (dst) *(void**)dst = src ? *(void**)src : nullptr;
}

bool __cdecl __ExceptionPtrToBool(const void* ep) noexcept {
    return (ep && *(void**)ep != nullptr);
}

void __cdecl __ExceptionPtrRethrow(const void*) {
    ExitProcess(0xC0000005);
}

void __cdecl __ExceptionPtrCopyException(void* dst, const void*, const void*) noexcept {
    if (dst) *(void**)dst = nullptr;
}

} // extern "C++"

namespace Concurrency {
namespace details {

static DWORD WINAPI _ChoreThreadProc(LPVOID param) {
    auto chore = (_Threadpool_chore*)param;
    if (chore && chore->_M_callback) {
        chore->_M_callback(chore->_M_data);
    }
    return 0;
}

int __cdecl _Schedule_chore(_Threadpool_chore* chore) {
    if (!chore) return -1;
    HANDLE hThread = CreateThread(nullptr, 0, _ChoreThreadProc, chore, 0, nullptr);
    if (hThread) {
        CloseHandle(hThread);
        return 0;
    }
    return -1;
}

void __cdecl _Release_chore(_Threadpool_chore*) {}

int __cdecl _Reschedule_chore(const _Threadpool_chore* chore) {
    return _Schedule_chore(const_cast<_Threadpool_chore*>(chore));
}

void __cdecl _ReportUnobservedException() {}

void __cdecl _TaskEventLogger::_LogScheduleTask(bool) {}
void __cdecl _TaskEventLogger::_LogTaskExecutionCompleted() {}
void __cdecl _TaskEventLogger::_LogTaskCompleted() {}
void __cdecl _TaskEventLogger::_LogWorkItemStarted() {}
void __cdecl _TaskEventLogger::_LogWorkItemCompleted() {}
void __cdecl _TaskEventLogger::_LogCancelTask() {}

void __cdecl _ContextCallback::_Capture() {}
void __cdecl _ContextCallback::_Reset() {}

void __cdecl _ContextCallback::_CallInContext(std::function<void()> f, bool) const {
    if (f) {
        f();
    }
}

namespace platform {
long __cdecl GetCurrentThreadId() {
    return (long)::GetCurrentThreadId();
}
} // namespace platform

void _MemLLVM_ForceEmitPPLChore() {
    auto chore = new _DefaultPPLTaskScheduler::_PPLTaskChore(nullptr, nullptr);
    delete chore;
}

} // namespace details

task_continuation_context::task_continuation_context() : details::_ContextCallback(true), _M_RunInline(false) {}

} // namespace Concurrency
