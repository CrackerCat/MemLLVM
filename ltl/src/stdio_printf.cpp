#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>

extern "C" {

int __cdecl __stdio_common_vfprintf(unsigned __int64 options, void* stream, const char* format, void* locale, va_list valist);
int __cdecl __stdio_common_vfwprintf(unsigned __int64 options, void* stream, const wchar_t* format, void* locale, va_list valist);
void* __cdecl __acrt_iob_func(unsigned int idx);

__attribute__((weak)) int __cdecl printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    int ret = __stdio_common_vfprintf(0, __acrt_iob_func(1), format, nullptr, args);
    va_end(args);
    return ret;
}

__attribute__((weak)) int __cdecl wprintf(const wchar_t* format, ...) {
    va_list args;
    va_start(args, format);
    int ret = __stdio_common_vfwprintf(0, __acrt_iob_func(1), format, nullptr, args);
    va_end(args);
    return ret;
}

// ============================================================================
// C++23 std::print / std::println Unicode Console Support
// ============================================================================

enum class __std_win_error : unsigned long {
    _Success = 0,
    _Invalid_parameter = 87,
    _Not_supported = 50,
    _File_not_found = 2
};

enum class __std_unicode_console_handle : intptr_t {
    _Invalid = -1
};

struct __std_unicode_console_retrieval_result {
    __std_unicode_console_handle _Console_handle = __std_unicode_console_handle::_Invalid;
    __std_win_error _Error = __std_win_error::_Invalid_parameter;
};

__std_unicode_console_retrieval_result __stdcall __std_get_unicode_console_handle_from_file_stream(void* const stream) noexcept {
    if (!stream) {
        return { __std_unicode_console_handle::_Invalid, __std_win_error::_Invalid_parameter };
    }
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    if (hOut && hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &mode)) {
        return { static_cast<__std_unicode_console_handle>(reinterpret_cast<intptr_t>(hOut)), __std_win_error::_Success };
    }
    return { __std_unicode_console_handle::_Invalid, __std_win_error::_File_not_found };
}

__std_win_error __stdcall __std_print_to_unicode_console(
    __std_unicode_console_handle handle, const char* str, size_t size) noexcept {
    if (!str || size == 0) return __std_win_error::_Success;
    HANDLE hConsole = reinterpret_cast<HANDLE>(static_cast<intptr_t>(handle));
    wchar_t wbuf[1024];
    wchar_t* target = wbuf;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, str, static_cast<int>(size), nullptr, 0);
    if (wlen <= 0) return __std_win_error::_Invalid_parameter;
    if (wlen > 1024) {
        target = static_cast<wchar_t*>(malloc(wlen * sizeof(wchar_t)));
        if (!target) return __std_win_error::_Not_supported;
    }
    MultiByteToWideChar(CP_UTF8, 0, str, static_cast<int>(size), target, wlen);
    DWORD written = 0;
    WriteConsoleW(hConsole, target, static_cast<DWORD>(wlen), &written, nullptr);
    if (target != wbuf) free(target);
    return __std_win_error::_Success;
}

}
