// ============================================================================
// MemLLVM-LTL CRT Memory Intrinsics (memcpy, memmove, memset)
// Fallback for LLVM CodeGen lowering in freestanding environments
// ============================================================================

#include <cstddef>

extern "C" {

int _fltused = 0x9875;

#pragma function(memcpy)
void* memcpy(void* dest, const void* src, size_t count) {
    char* d = static_cast<char*>(dest);
    const char* s = static_cast<const char*>(src);
    for (size_t i = 0; i < count; ++i) {
        d[i] = s[i];
    }
    return dest;
}

#pragma function(memmove)
void* memmove(void* dest, const void* src, size_t count) {
    char* d = static_cast<char*>(dest);
    const char* s = static_cast<const char*>(src);
    if (d < s) {
        for (size_t i = 0; i < count; ++i) {
            d[i] = s[i];
        }
    } else if (d > s) {
        for (size_t i = count; i > 0; --i) {
            d[i - 1] = s[i - 1];
        }
    }
    return dest;
}

#pragma function(memset)
void* memset(void* dest, int c, size_t count) {
    char* d = static_cast<char*>(dest);
    for (size_t i = 0; i < count; ++i) {
        d[i] = static_cast<char>(c);
    }
    return dest;
}

size_t __stdcall __std_mismatch_1(const void* _First1, const void* _First2, size_t _Count) noexcept {
    const unsigned char* p1 = static_cast<const unsigned char*>(_First1);
    const unsigned char* p2 = static_cast<const unsigned char*>(_First2);
    for (size_t i = 0; i < _Count; ++i) if (p1[i] != p2[i]) return i;
    return _Count;
}

size_t __stdcall __std_mismatch_2(const void* _First1, const void* _First2, size_t _Count) noexcept {
    const unsigned short* p1 = static_cast<const unsigned short*>(_First1);
    const unsigned short* p2 = static_cast<const unsigned short*>(_First2);
    for (size_t i = 0; i < _Count; ++i) if (p1[i] != p2[i]) return i;
    return _Count;
}

size_t __stdcall __std_mismatch_4(const void* _First1, const void* _First2, size_t _Count) noexcept {
    const unsigned int* p1 = static_cast<const unsigned int*>(_First1);
    const unsigned int* p2 = static_cast<const unsigned int*>(_First2);
    for (size_t i = 0; i < _Count; ++i) if (p1[i] != p2[i]) return i;
    return _Count;
}

size_t __stdcall __std_mismatch_8(const void* _First1, const void* _First2, size_t _Count) noexcept {
    const unsigned long long* p1 = static_cast<const unsigned long long*>(_First1);
    const unsigned long long* p2 = static_cast<const unsigned long long*>(_First2);
    for (size_t i = 0; i < _Count; ++i) if (p1[i] != p2[i]) return i;
    return _Count;
}

} // extern "C"

namespace std {
const char* __cdecl _Syserror_map(int) {
    return "system error";
}
} // namespace std
