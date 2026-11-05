// ============================================================================
// MemLLVM-LTL Atomic Wait & Synchronization Runtime
// std::atomic::wait/notify, std::latch, std::barrier, std::semaphore
// Dynamic WaitOnAddress/WakeByAddress binding with universal fallback
// ============================================================================

#include "ltl_config.h"
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <intrin.h>

#pragma bss_seg(".data")
#pragma data_seg(".data")

using _Smtx_t = void*;

typedef BOOL (WINAPI *pfnWaitOnAddress)(volatile VOID*, PVOID, SIZE_T, DWORD);
typedef VOID (WINAPI *pfnWakeByAddressSingle)(PVOID);
typedef VOID (WINAPI *pfnWakeByAddressAll)(PVOID);

static pfnWaitOnAddress _g_WaitOnAddress = nullptr;
static pfnWakeByAddressSingle _g_WakeByAddressSingle = nullptr;
static pfnWakeByAddressAll _g_WakeByAddressAll = nullptr;
static bool _g_atomic_wait_inited = false;

static void _init_atomic_wait_apis() noexcept {
    if (_g_atomic_wait_inited) return;
    _g_atomic_wait_inited = true;

    HMODULE hKb = GetModuleHandleA("kernelbase.dll");
    if (!hKb) hKb = LoadLibraryA("kernelbase.dll");
    if (hKb) {
        _g_WaitOnAddress = reinterpret_cast<pfnWaitOnAddress>(GetProcAddress(hKb, "WaitOnAddress"));
        _g_WakeByAddressSingle = reinterpret_cast<pfnWakeByAddressSingle>(GetProcAddress(hKb, "WakeByAddressSingle"));
        _g_WakeByAddressAll = reinterpret_cast<pfnWakeByAddressAll>(GetProcAddress(hKb, "WakeByAddressAll"));
    }
}

namespace {
    struct _Wait_context {
        _Wait_context* _Next;
        _Wait_context* _Prev;
        const void* _Storage;
        CONDITION_VARIABLE _Condition;
    };

    struct _Table_entry {
        SRWLOCK _Lock;
        _Wait_context _Wait_list_head;
    };

    constexpr size_t _Wait_table_size_power = 6;
    constexpr size_t _Wait_table_size = 1 << _Wait_table_size_power;
    constexpr size_t _Wait_table_index_mask = _Wait_table_size - 1;

    static _Table_entry _g_wait_table[_Wait_table_size] = {};

    static _Table_entry& _get_table_entry(const void* storage) noexcept {
        auto index = reinterpret_cast<uintptr_t>(storage);
        index ^= index >> (_Wait_table_size_power * 2);
        index ^= index >> _Wait_table_size_power;
        return _g_wait_table[index & _Wait_table_index_mask];
    }
}

extern "C" {

#if (defined(__cplusplus) && __cplusplus >= 201703L) || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)
using _Atomic_wait_indirect_equal_callback_t = bool(__stdcall*)(
    const void* _Storage, void* _Comparand, size_t _Size, void* _Param) noexcept;
#else
using _Atomic_wait_indirect_equal_callback_t = bool(__stdcall*)(
    const void* _Storage, void* _Comparand, size_t _Size, void* _Param);
#endif

int __stdcall __std_atomic_wait_direct(
    const void* const _Storage, void* const _Comparand, const size_t _Size,
    const unsigned long _Remaining_timeout) noexcept {
    _init_atomic_wait_apis();
    if (_g_WaitOnAddress) {
        return _g_WaitOnAddress(const_cast<volatile void*>(_Storage), _Comparand, _Size, _Remaining_timeout);
    }
    // Universal Fallback if WaitOnAddress is absent
    DWORD start = GetTickCount();
    while (memcmp(const_cast<void*>(_Storage), _Comparand, _Size) == 0) {
        if (_Remaining_timeout != INFINITE && (GetTickCount() - start) >= _Remaining_timeout) {
            return 0; // Timeout
        }
        Sleep(1);
    }
    return 1;
}

void __stdcall __std_atomic_notify_one_direct(const void* const _Storage) noexcept {
    _init_atomic_wait_apis();
    if (_g_WakeByAddressSingle) {
        _g_WakeByAddressSingle(const_cast<void*>(_Storage));
    }
}

void __stdcall __std_atomic_notify_all_direct(const void* const _Storage) noexcept {
    _init_atomic_wait_apis();
    if (_g_WakeByAddressAll) {
        _g_WakeByAddressAll(const_cast<void*>(_Storage));
    }
}

void __stdcall __std_atomic_notify_one_indirect(const void* const _Storage) noexcept {
    auto& entry = _get_table_entry(_Storage);
    AcquireSRWLockExclusive(&entry._Lock);
    _Wait_context* ctx = entry._Wait_list_head._Next;
    if (ctx) {
        for (; ctx != &entry._Wait_list_head; ctx = ctx->_Next) {
            if (ctx->_Storage == _Storage) {
                WakeAllConditionVariable(&ctx->_Condition);
                break;
            }
        }
    }
    ReleaseSRWLockExclusive(&entry._Lock);
}

void __stdcall __std_atomic_notify_all_indirect(const void* const _Storage) noexcept {
    auto& entry = _get_table_entry(_Storage);
    AcquireSRWLockExclusive(&entry._Lock);
    _Wait_context* ctx = entry._Wait_list_head._Next;
    if (ctx) {
        for (; ctx != &entry._Wait_list_head; ctx = ctx->_Next) {
            if (ctx->_Storage == _Storage) {
                WakeAllConditionVariable(&ctx->_Condition);
            }
        }
    }
    ReleaseSRWLockExclusive(&entry._Lock);
}

int __stdcall __std_atomic_wait_indirect(
    const void* _Storage, void* _Comparand, size_t _Size, void* _Param,
    _Atomic_wait_indirect_equal_callback_t _Are_equal, unsigned long _Remaining_timeout) noexcept {
    auto& entry = _get_table_entry(_Storage);
    AcquireSRWLockExclusive(&entry._Lock);

    if (entry._Wait_list_head._Next == nullptr) {
        entry._Wait_list_head._Next = &entry._Wait_list_head;
        entry._Wait_list_head._Prev = &entry._Wait_list_head;
    }

    _Wait_context ctx{};
    ctx._Storage = _Storage;
    // Insert into doubly linked list
    ctx._Next = entry._Wait_list_head._Next;
    ctx._Prev = &entry._Wait_list_head;
    entry._Wait_list_head._Next->_Prev = &ctx;
    entry._Wait_list_head._Next = &ctx;

    int result = 1;
    for (;;) {
        if (!_Are_equal(_Storage, _Comparand, _Size, _Param)) {
            result = 1;
            break;
        }

        if (!SleepConditionVariableSRW(&ctx._Condition, &entry._Lock, _Remaining_timeout, 0)) {
            result = 0; // timeout
            break;
        }

        if (_Remaining_timeout != 0xFFFFFFFF) {
            result = 1;
            break;
        }
    }

    // Unlink context
    ctx._Prev->_Next = ctx._Next;
    ctx._Next->_Prev = ctx._Prev;

    ReleaseSRWLockExclusive(&entry._Lock);
    return result;
}

unsigned long long __stdcall __std_atomic_wait_get_deadline(const unsigned long long _Timeout) noexcept {
    if (_Timeout == 0xFFFFFFFFFFFFFFFFULL) {
        return 0xFFFFFFFFFFFFFFFFULL;
    }
    return GetTickCount64() + _Timeout;
}

unsigned long __stdcall __std_atomic_wait_get_remaining_timeout(unsigned long long _Deadline) noexcept {
    if (_Deadline == 0xFFFFFFFFFFFFFFFFULL) {
        return 0xFFFFFFFF;
    }
    unsigned long long now = GetTickCount64();
    if (now >= _Deadline) return 0;
    unsigned long long remaining = _Deadline - now;
    constexpr unsigned long ten_days = 864000000;
    if (remaining > ten_days) return ten_days;
    return static_cast<unsigned long>(remaining);
}

enum class __std_atomic_api_level : unsigned long { __not_set, __detecting, __has_srwlock, __has_wait_on_address };
__std_atomic_api_level __stdcall __std_atomic_set_api_level(__std_atomic_api_level) noexcept {
    return __std_atomic_api_level::__has_wait_on_address;
}

static _Smtx_t _g_atomic_mutex_table[64] = { nullptr };

_Smtx_t* __stdcall __std_atomic_get_mutex(const void* const _Key) noexcept {
    auto index = reinterpret_cast<uintptr_t>(_Key);
    index ^= index >> 12;
    index ^= index >> 6;
    return &_g_atomic_mutex_table[index & 63];
}

unsigned char __stdcall __std_atomic_compare_exchange_128(
    long long* _Destination, long long _ExchangeHigh, long long _ExchangeLow,
    long long* _ComparandResult) noexcept {
#ifdef _WIN64
    unsigned char result;
    __asm__ volatile(
        "lock cmpxchg16b %1\n\t"
        "setz %0"
        : "=q"(result), "+m"(*_Destination), "+d"(_ComparandResult[1]), "+a"(_ComparandResult[0])
        : "c"(_ExchangeHigh), "b"(_ExchangeLow)
        : "memory", "cc"
    );
    return result;
#else
    return 0;
#endif
}

#ifdef _WIN64
bool __atomic_compare_exchange_16(volatile void* obj, void* expected, unsigned __int128 desired, int, int) noexcept {
    unsigned long long* exp = reinterpret_cast<unsigned long long*>(expected);
    unsigned long long low = static_cast<unsigned long long>(desired);
    unsigned long long high = static_cast<unsigned long long>(desired >> 64);
    unsigned char result;
    __asm__ volatile(
        "lock cmpxchg16b %1\n\t"
        "setz %0"
        : "=q"(result), "+m"(*reinterpret_cast<volatile long long*>(obj)), "+d"(exp[1]), "+a"(exp[0])
        : "c"(high), "b"(low)
        : "memory", "cc"
    );
    return result != 0;
}
#endif

char __stdcall __std_atomic_has_cmpxchg16b() noexcept {
#ifdef _WIN64
    return 1;
#else
    return 0;
#endif
}

} // extern "C"
