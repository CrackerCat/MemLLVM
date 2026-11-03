#include <windows.h>
#include <cstdarg>

extern "C" {

int __cdecl __stdio_common_vfprintf(unsigned __int64 options, void* stream, const char* format, void* locale, va_list valist);
void* __cdecl __acrt_iob_func(unsigned int idx);

__attribute__((weak)) int __cdecl puts(const char* s) {
    return __stdio_common_vfprintf(0, __acrt_iob_func(1), "%s\n", nullptr, (va_list)&s);
}

}
