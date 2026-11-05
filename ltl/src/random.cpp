// ============================================================================
// MemLLVM-LTL Random Device Runtime (std::random_device for Flat Machine Code)
// Bridges std::random_device directly to Win32 SystemFunction036 / BCrypt
// ============================================================================

#include "ltl_config.h"
#include <windows.h>
#include <cstdint>

#pragma bss_seg(".data")
#pragma data_seg(".data")

namespace std {

unsigned int __cdecl _Random_device(void) noexcept {
    unsigned int val = 0;
    // Attempt SystemFunction036 (RtlGenRandom in advapi32.dll)
    HMODULE hAdvapi = LoadLibraryA("advapi32.dll");
    if (hAdvapi) {
        typedef BOOLEAN (APIENTRY *RtlGenRandomFn)(void*, ULONG);
        auto fn = reinterpret_cast<RtlGenRandomFn>(GetProcAddress(hAdvapi, "SystemFunction036"));
        if (fn && fn(&val, sizeof(val))) {
            return val;
        }
    }
    // High-entropy fallback: CPU timestamp counter ^ system tick count
    unsigned __int64 tsc = __rdtsc();
    return static_cast<unsigned int>(tsc ^ (tsc >> 32) ^ GetTickCount());
}

} // namespace std
