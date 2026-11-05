#include <windows.h>
#include <cstdarg>

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

}
