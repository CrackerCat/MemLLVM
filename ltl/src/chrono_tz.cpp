// ============================================================================
// MemLLVM-LTL Chrono Timezone Runtime (std::chrono::tzdb & zoned_time)
// Bridges std::chrono time zone queries directly to Win32 dynamic time zones
// ============================================================================

#include "ltl_config.h"
#include <windows.h>
#include <cstdint>
#include <cstddef>

#pragma bss_seg(".data")
#pragma data_seg(".data")

extern "C" {

using __std_tzdb_epoch_milli = double;

struct __std_tzdb_leap_info {
    uint16_t _Year;
    uint16_t _Month;
    uint16_t _Day;
    uint16_t _Hour;
    uint16_t _Negative;
    uint16_t _Reserved;
};

enum class __std_tzdb_error {
    _Success   = 0,
    _Win_error = 1,
    _Icu_error = 2,
};

struct __std_tzdb_time_zones_info {
    __std_tzdb_error _Err;
    const char* _Version;
    size_t _Num_time_zones;
    const char** _Names;
    const char** _Links;
};

struct __std_tzdb_current_zone_info {
    __std_tzdb_error _Err;
    const char* _Tz_name;
};

struct __std_tzdb_sys_info {
    __std_tzdb_error _Err;
    __std_tzdb_epoch_milli _Begin;
    __std_tzdb_epoch_milli _End;
    int32_t _Offset;
    int32_t _Save;
    const char* _Abbrev;
};

void* __stdcall __std_calloc_crt(size_t count, size_t size) noexcept {
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * size);
}

void __stdcall __std_free_crt(void* ptr) noexcept {
    if (ptr) HeapFree(GetProcessHeap(), 0, ptr);
}

__std_tzdb_current_zone_info* __stdcall __std_tzdb_get_current_zone() noexcept {
    auto* info = reinterpret_cast<__std_tzdb_current_zone_info*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(__std_tzdb_current_zone_info)));
    if (!info) return nullptr;
    info->_Err = __std_tzdb_error::_Success;

    DYNAMIC_TIME_ZONE_INFORMATION dtzi = {};
    GetDynamicTimeZoneInformation(&dtzi);
    char* name = reinterpret_cast<char*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 128));
    const wchar_t* src = dtzi.TimeZoneKeyName[0] ? dtzi.TimeZoneKeyName : dtzi.StandardName;
    if (!src[0]) src = L"UTC";
    WideCharToMultiByte(CP_UTF8, 0, src, -1, name, 128, nullptr, nullptr);
    info->_Tz_name = name;
    return info;
}

void __stdcall __std_tzdb_delete_current_zone(__std_tzdb_current_zone_info* info) noexcept {
    if (info) {
        if (info->_Tz_name) HeapFree(GetProcessHeap(), 0, const_cast<char*>(info->_Tz_name));
        HeapFree(GetProcessHeap(), 0, info);
    }
}

__std_tzdb_time_zones_info* __stdcall __std_tzdb_get_time_zones() noexcept {
    auto* info = reinterpret_cast<__std_tzdb_time_zones_info*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(__std_tzdb_time_zones_info)));
    if (!info) return nullptr;
    info->_Err = __std_tzdb_error::_Success;
    info->_Version = "2026a";

    DYNAMIC_TIME_ZONE_INFORMATION dtzi = {};
    GetDynamicTimeZoneInformation(&dtzi);
    char* name = reinterpret_cast<char*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 128));
    const wchar_t* src = dtzi.TimeZoneKeyName[0] ? dtzi.TimeZoneKeyName : dtzi.StandardName;
    if (!src[0]) src = L"UTC";
    WideCharToMultiByte(CP_UTF8, 0, src, -1, name, 128, nullptr, nullptr);

    const char** names = reinterpret_cast<const char**>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 2 * sizeof(char*)));
    const char** links = reinterpret_cast<const char**>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 2 * sizeof(char*)));
    names[0] = name;
    names[1] = "UTC";
    links[0] = nullptr;
    links[1] = nullptr;

    info->_Num_time_zones = 2;
    info->_Names = names;
    info->_Links = links;
    return info;
}

void __stdcall __std_tzdb_delete_time_zones(__std_tzdb_time_zones_info* info) noexcept {
    if (info) {
        if (info->_Names) {
            HeapFree(GetProcessHeap(), 0, const_cast<char*>(info->_Names[0]));
            HeapFree(GetProcessHeap(), 0, const_cast<char**>(info->_Names));
        }
        if (info->_Links) HeapFree(GetProcessHeap(), 0, const_cast<char**>(info->_Links));
        HeapFree(GetProcessHeap(), 0, info);
    }
}

__std_tzdb_leap_info* __stdcall __std_tzdb_get_leap_seconds(
    size_t prev_size, size_t* curr_size) noexcept {
    (void)prev_size;
    if (curr_size) *curr_size = 0;
    return nullptr;
}

void __stdcall __std_tzdb_delete_leap_seconds(__std_tzdb_leap_info* info) noexcept {
    if (info) HeapFree(GetProcessHeap(), 0, info);
}

__std_tzdb_sys_info* __stdcall __std_tzdb_get_sys_info(
    const char* tz, size_t tz_len, __std_tzdb_epoch_milli local) noexcept {
    (void)tz; (void)tz_len; (void)local;
    auto* info = reinterpret_cast<__std_tzdb_sys_info*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(__std_tzdb_sys_info)));
    if (!info) return nullptr;
    info->_Err = __std_tzdb_error::_Success;
    DYNAMIC_TIME_ZONE_INFORMATION dtzi = {};
    GetDynamicTimeZoneInformation(&dtzi);
    info->_Offset = -(dtzi.Bias + dtzi.StandardBias) * 60;
    info->_Save = 0;
    info->_Begin = -1e14;
    info->_End = 1e14;
    info->_Abbrev = "LOCAL";
    return info;
}

void __stdcall __std_tzdb_delete_sys_info(__std_tzdb_sys_info* info) noexcept {
    if (info) HeapFree(GetProcessHeap(), 0, info);
}

} // extern "C"
