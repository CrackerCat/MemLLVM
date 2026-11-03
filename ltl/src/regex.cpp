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
    size_t count = (const uint8_t*)_Last - (const uint8_t*)_First;
    if (!count) return _Last;
    const void* res = _First;
    __asm__ volatile(
        "repne scasb\n\t"
        "jnz 1f\n\t"
        "decq %%rdi\n\t"
        "movq %%rdi, %0\n\t"
        "jmp 2f\n\t"
        "1:\n\t"
        "movq %1, %0\n\t"
        "2:\n\t"
        : "=r"(res)
        : "r"(_Last), "0"(res), "a"(_Val), "c"(count), "D"(res)
        : "cc"
    );
    return res;
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
