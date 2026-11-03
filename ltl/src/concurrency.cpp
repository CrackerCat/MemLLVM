// ============================================================================
// MemLLVM-LTL Concurrency Runtime (std::mutex, std::condition_variable, std::thread)
// Direct SRWLock & CreateThread mapping for zero-dependency flat machine code
// ============================================================================

#include "ltl_config.h"
#include <windows.h>
#include <xthreads.h>
#include <cstdint>

#pragma bss_seg(".data")
#pragma data_seg(".data")

static PSRWLOCK get_srw_lock(_Mtx_t mtx) noexcept {
    return reinterpret_cast<PSRWLOCK>(&mtx->_Critical_section._M_srw_lock);
}

static PCONDITION_VARIABLE get_cv(_Cnd_t cond) noexcept {
    return reinterpret_cast<PCONDITION_VARIABLE>(&cond->_Stl_cv._Win_cv);
}

extern "C" {

// ----------------------------------------------------------------------------
// Mutex implementation
// ----------------------------------------------------------------------------

static inline long get_cur_thread_id() noexcept {
    unsigned long tid;
    __asm__ volatile("movl %%gs:0x48, %0" : "=r"(tid));
    return static_cast<long>(tid);
}

void __cdecl _Mtx_init_in_situ(_Mtx_t mtx, int type) noexcept {
    mtx->_Type = type;
    mtx->_Critical_section._Unused = nullptr;
    mtx->_Critical_section._M_srw_lock = nullptr; // SRWLOCK_INIT
    mtx->_Thread_id = -1;
    mtx->_Count = 0;
}

void __cdecl _Mtx_destroy_in_situ(_Mtx_t) noexcept {}

int __cdecl _Mtx_current_owns(_Mtx_t mtx) noexcept {
    long cur_id = get_cur_thread_id();
    return (mtx && mtx->_Thread_id == cur_id) ? 1 : 0;
}

_Thrd_result __cdecl _Mtx_lock(_Mtx_t mtx) noexcept {
    long cur_id = get_cur_thread_id();
    if (mtx->_Type == _Mtx_plain || mtx->_Type == (_Mtx_plain | _Mtx_try)) {
        if (mtx->_Thread_id != cur_id) {
            AcquireSRWLockExclusive(get_srw_lock(mtx));
            mtx->_Thread_id = cur_id;
        }
        ++mtx->_Count;
        return _Thrd_result::_Success;
    } else { // Recursive / timed mutex
        if (mtx->_Thread_id == cur_id) {
            ++mtx->_Count;
            return _Thrd_result::_Success;
        }
        AcquireSRWLockExclusive(get_srw_lock(mtx));
        mtx->_Thread_id = cur_id;
        mtx->_Count = 1;
        return _Thrd_result::_Success;
    }
}

_Thrd_result __cdecl _Mtx_trylock(_Mtx_t mtx) noexcept {
    long cur_id = get_cur_thread_id();
    if (mtx->_Type != _Mtx_plain && mtx->_Type != (_Mtx_plain | _Mtx_try) && mtx->_Thread_id == cur_id) {
        ++mtx->_Count;
        return _Thrd_result::_Success;
    }
    if (TryAcquireSRWLockExclusive(get_srw_lock(mtx))) {
        mtx->_Thread_id = cur_id;
        mtx->_Count = 1;
        return _Thrd_result::_Success;
    }
    return _Thrd_result::_Busy;
}

_Thrd_result __cdecl _Mtx_timedlock(_Mtx_t mtx, const void*) noexcept {
    return _Mtx_lock(mtx);
}

_Thrd_result __cdecl _Mtx_unlock(_Mtx_t mtx) noexcept {
    if (--mtx->_Count == 0) {
        mtx->_Thread_id = -1;
        ReleaseSRWLockExclusive(get_srw_lock(mtx));
    }
    return _Thrd_result::_Success;
}

// ----------------------------------------------------------------------------
// Shared Mutex (SRWLock) implementation
// ----------------------------------------------------------------------------

void __cdecl _Smtx_lock_exclusive(_Smtx_t* smtx) noexcept {
    AcquireSRWLockExclusive(reinterpret_cast<PSRWLOCK>(smtx));
}

void __cdecl _Smtx_lock_shared(_Smtx_t* smtx) noexcept {
    AcquireSRWLockShared(reinterpret_cast<PSRWLOCK>(smtx));
}

int __cdecl _Smtx_try_lock_exclusive(_Smtx_t* smtx) noexcept {
    return TryAcquireSRWLockExclusive(reinterpret_cast<PSRWLOCK>(smtx)) ? 1 : 0;
}

int __cdecl _Smtx_try_lock_shared(_Smtx_t* smtx) noexcept {
    return TryAcquireSRWLockShared(reinterpret_cast<PSRWLOCK>(smtx)) ? 1 : 0;
}

void __cdecl _Smtx_unlock_exclusive(_Smtx_t* smtx) noexcept {
    ReleaseSRWLockExclusive(reinterpret_cast<PSRWLOCK>(smtx));
}

void __cdecl _Smtx_unlock_shared(_Smtx_t* smtx) noexcept {
    ReleaseSRWLockShared(reinterpret_cast<PSRWLOCK>(smtx));
}

// ----------------------------------------------------------------------------
// Condition Variable implementation
// ----------------------------------------------------------------------------

void __cdecl _Cnd_init_in_situ(_Cnd_t cond) noexcept {
    cond->_Stl_cv._Unused = nullptr;
    cond->_Stl_cv._Win_cv = nullptr; // CONDITION_VARIABLE_INIT
}

void __cdecl _Cnd_destroy_in_situ(_Cnd_t) noexcept {}

_Thrd_result __cdecl _Cnd_wait(_Cnd_t cond, _Mtx_t mtx) noexcept {
    long old_id = mtx->_Thread_id;
    int old_count = mtx->_Count;
    mtx->_Thread_id = -1;
    mtx->_Count = 0;
    SleepConditionVariableSRW(get_cv(cond), get_srw_lock(mtx), INFINITE, 0);
    mtx->_Thread_id = old_id;
    mtx->_Count = old_count;
    return _Thrd_result::_Success;
}

_Thrd_result __stdcall _Cnd_timedwait_for_unchecked(_Cnd_t cond, _Mtx_t mtx, unsigned int ms) noexcept {
    long old_id = mtx->_Thread_id;
    int old_count = mtx->_Count;
    mtx->_Thread_id = -1;
    mtx->_Count = 0;
    BOOL ok = SleepConditionVariableSRW(get_cv(cond), get_srw_lock(mtx), ms, 0);
    mtx->_Thread_id = old_id;
    mtx->_Count = old_count;
    if (!ok) {
        if (GetLastError() == ERROR_TIMEOUT) return _Thrd_result::_Timedout;
        return _Thrd_result::_Error;
    }
    return _Thrd_result::_Success;
}

_Thrd_result __cdecl _Cnd_signal(_Cnd_t cond) noexcept {
    WakeConditionVariable(get_cv(cond));
    return _Thrd_result::_Success;
}

_Thrd_result __cdecl _Cnd_broadcast(_Cnd_t cond) noexcept {
    WakeAllConditionVariable(get_cv(cond));
    return _Thrd_result::_Success;
}

void __cdecl _Cnd_register_at_thread_exit(_Cnd_t, _Mtx_t, int*) noexcept {}
void __cdecl _Cnd_unregister_at_thread_exit(_Mtx_t) noexcept {}
void __cdecl _Cnd_do_broadcast_at_thread_exit() noexcept {}

// ----------------------------------------------------------------------------
// Thread implementation
// ----------------------------------------------------------------------------

uintptr_t __cdecl _beginthreadex(
    void* security,
    unsigned stack_size,
    unsigned (__stdcall *start_address)(void*),
    void* arglist,
    unsigned initflag,
    unsigned* thrdaddr) {
    DWORD thread_id = 0;
    HANDLE hThread = CreateThread(
        static_cast<LPSECURITY_ATTRIBUTES>(security),
        stack_size,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(start_address),
        arglist,
        initflag,
        &thread_id);
    if (thrdaddr) *thrdaddr = thread_id;
    return reinterpret_cast<uintptr_t>(hThread);
}

_Thrd_result __cdecl _Thrd_join(_Thrd_t thr, int* code) noexcept {
    if (WaitForSingleObjectEx(thr._Hnd, INFINITE, FALSE) == WAIT_FAILED) {
        return _Thrd_result::_Error;
    }
    if (code) {
        DWORD exitCode = 0;
        GetExitCodeThread(thr._Hnd, &exitCode);
        *code = static_cast<int>(exitCode);
    }
    CloseHandle(thr._Hnd);
    return _Thrd_result::_Success;
}

_Thrd_result __cdecl _Thrd_detach(_Thrd_t thr) noexcept {
    CloseHandle(thr._Hnd);
    return _Thrd_result::_Success;
}

_Thrd_id_t __cdecl _Thrd_id() noexcept {
    return static_cast<unsigned int>(get_cur_thread_id());
}

void __cdecl _Thrd_yield() noexcept {
    Sleep(0);
}

void __stdcall _Thrd_sleep_for(unsigned long ms) noexcept {
    Sleep(ms);
}

unsigned int __cdecl _Thrd_hardware_concurrency() noexcept {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors ? si.dwNumberOfProcessors : 1;
}

[[noreturn]] void __cdecl _Thrd_abort(const char*) noexcept {
    ExitProcess(0xC0000005);
}

[[noreturn]] void __cdecl _Thrd_exit(int res) noexcept {
    ExitThread(static_cast<DWORD>(res));
}

} // extern "C"

namespace std {
void __cdecl _Throw_Cpp_error(int) {
    ExitProcess(0xC0000005);
}
} // namespace std
