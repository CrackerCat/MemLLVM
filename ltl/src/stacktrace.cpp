// ============================================================================
// MemLLVM-LTL C++23 Stacktrace Runtime (std::stacktrace)
// Freestanding Stack Walking & Address String Formatting for Flat Binaries
// ============================================================================

#include "ltl_config.h"
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>

#pragma bss_seg(".data")
#pragma data_seg(".data")

extern "C" {

#if (defined(__cplusplus) && __cplusplus >= 201703L) || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)
using _Stacktrace_string_fill_callback = size_t(__stdcall*)(char* _Data, size_t _Size, void* _Context) noexcept;
#else
using _Stacktrace_string_fill_callback = size_t(__stdcall*)(char* _Data, size_t _Size, void* _Context);
#endif

using _Stacktrace_string_fill = size_t(__stdcall*)(
    size_t _Size, void* _String, void* _Context, _Stacktrace_string_fill_callback _Callback);

struct _CopyContext {
    const char* str;
    size_t len;
};

static size_t __stdcall _stacktrace_copy_cb(char* data, size_t sz, void* ctx) noexcept {
    auto c = static_cast<_CopyContext*>(ctx);
    size_t to_copy = (c->len < sz) ? c->len : sz;
    if (data && c->str && to_copy > 0) {
        memcpy(data, c->str, to_copy);
    }
    return to_copy;
}

#include <intrin.h>

unsigned short __stdcall __std_stacktrace_capture(
    unsigned long _Frames_to_skip, unsigned long _Frames_to_capture,
    void** const _Back_trace, unsigned long* const _Back_trace_hash) noexcept {
    unsigned short captured = RtlCaptureStackBackTrace(_Frames_to_skip, _Frames_to_capture, _Back_trace, _Back_trace_hash);
    if (captured == 0 && _Frames_to_capture > 0 && _Back_trace) {
        void* caller = _ReturnAddress();
        _Back_trace[0] = caller;
        captured = 1;
        if (_Back_trace_hash) {
            *_Back_trace_hash = static_cast<unsigned long>(reinterpret_cast<uintptr_t>(caller));
        }
    }
    return captured;
}

void __stdcall __std_stacktrace_address_to_string(
    const void* const _Address, void* const _Str, const _Stacktrace_string_fill _Fill) noexcept {
    if (!_Fill) return;
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "0x%016llX", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(_Address)));
    if (n > 0) {
        _CopyContext ctx{ buf, static_cast<size_t>(n) };
        _Fill(static_cast<size_t>(n), _Str, &ctx, _stacktrace_copy_cb);
    }
}

void __stdcall __std_stacktrace_description(
    const void* const _Address, void* const _Str, const _Stacktrace_string_fill _Fill) noexcept {
    __std_stacktrace_address_to_string(_Address, _Str, _Fill);
}

void __stdcall __std_stacktrace_source_file(
    const void* const _Address, void* const _Str, const _Stacktrace_string_fill _Fill) noexcept {
    if (!_Fill) return;
    _CopyContext ctx{ "", 0 };
    _Fill(0, _Str, &ctx, _stacktrace_copy_cb);
}

unsigned int __stdcall __std_stacktrace_source_line(const void* const) noexcept {
    return 0;
}

void __stdcall __std_stacktrace_to_string(
    const void* const* const _Addresses, const size_t _Size, void* const _Str,
    const _Stacktrace_string_fill _Fill) noexcept {
    if (!_Fill || !_Addresses || _Size == 0) return;

    char buf[4096];
    size_t off = 0;
    for (size_t i = 0; i < _Size && off + 48 < sizeof(buf); ++i) {
        if (off != 0) {
            buf[off++] = '\n';
        }
        int n = snprintf(buf + off, sizeof(buf) - off, "%u> 0x%016llX",
            static_cast<unsigned int>(i),
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(_Addresses[i])));
        if (n > 0) {
            off += static_cast<size_t>(n);
        }
    }
    buf[off] = '\0';
    _CopyContext ctx{ buf, off };
    _Fill(off, _Str, &ctx, _stacktrace_copy_cb);
}

} // extern "C"
