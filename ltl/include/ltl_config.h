#pragma once

// ============================================================================
// MemLLVM-LTL: Lightweight Toolset Library Configuration
// Inspired by VC-LTL architecture for zero-dependency flat machine code
// ============================================================================

#ifndef _CRT_WINDOWS_USE_VISTA_TSS
#define _CRT_WINDOWS_USE_VISTA_TSS 1
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601 // Windows 7 SP1+
#endif

#ifndef _HAS_EXCEPTIONS
#define _HAS_EXCEPTIONS 0
#endif

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS 1
#endif

#ifndef _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING 1
#endif

#ifndef _CRT_DISABLE_PERFCRIT_LOCKS
#define _CRT_DISABLE_PERFCRIT_LOCKS 1
#endif
