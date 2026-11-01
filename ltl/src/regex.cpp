// ============================================================================
// MemLLVM-LTL Regex & Algorithmic Search Runtime
// Lightweight, freestanding implementation of __std search primitives
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <cstring>

#pragma bss_seg(".data")
#pragma data_seg(".data")

extern "C" {

const void* __stdcall __std_find_trivial_1(const void* _First, const void* _Last, uint8_t _Val) noexcept {
    const uint8_t* cur = (const uint8_t*)_First;
    const uint8_t* end = (const uint8_t*)_Last;
    while (cur != end) {
        if (*cur == _Val) return cur;
        ++cur;
    }
    return end;
}

const void* __stdcall __std_find_first_of_trivial_1(
    const void* _First1, const void* _Last1, const void* _First2, const void* _Last2) noexcept {
    const uint8_t* cur1 = (const uint8_t*)_First1;
    const uint8_t* end1 = (const uint8_t*)_Last1;
    const uint8_t* beg2 = (const uint8_t*)_First2;
    const uint8_t* end2 = (const uint8_t*)_Last2;

    while (cur1 != end1) {
        for (const uint8_t* p2 = beg2; p2 != end2; ++p2) {
            if (*cur1 == *p2) return cur1;
        }
        ++cur1;
    }
    return end1;
}

const void* __stdcall __std_search_1(
    const void* _First1, const void* _Last1, const void* _First2, size_t _Count2) noexcept {
    if (_Count2 == 0) return _First1;
    const uint8_t* cur1 = (const uint8_t*)_First1;
    const uint8_t* end1 = (const uint8_t*)_Last1;
    const uint8_t* target = (const uint8_t*)_First2;

    size_t len1 = end1 - cur1;
    if (len1 < _Count2) return end1;

    const uint8_t* search_end = end1 - _Count2 + 1;
    while (cur1 != search_end) {
        if (memcmp(cur1, target, _Count2) == 0) {
            return cur1;
        }
        ++cur1;
    }
    return end1;
}

} // extern "C"
