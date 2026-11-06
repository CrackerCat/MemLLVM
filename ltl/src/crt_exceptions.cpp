//
// crt_exceptions.cpp -- MemLLVM C++ Exception Handling Runtime & SEH Dispatcher
//
// Provides personality routine forwarding (__CxxFrameHandler3/4, __C_specific_handler),
// exception throwing (_CxxThrowException), and dynamic table registration via RtlAddFunctionTable.
//

#include <stddef.h>
#include <stdint.h>

extern "C" {

// Magic signature for extract_flat_bin.py to locate this structure: "MEMLLVME"
#define MEMLLVM_PDATA_MAGIC 0x4D454D4C4C564D45ULL

struct MemLLVM_PDataInfo {
    uint64_t magic;
    void*    image_base;
    void*    pdata_ptr;
    uint32_t pdata_count;
    uint32_t flags;
};

// Global metadata descriptor populated by the bootstrap stub or fallback
__declspec(dllexport) volatile MemLLVM_PDataInfo g_memllvm_pdata_info = {
    MEMLLVM_PDATA_MAGIC,
    nullptr,
    nullptr,
    0,
    0
};
#pragma comment(linker, "/include:g_memllvm_pdata_info")

// Fallback image base
uintptr_t g_memllvm_image_base = 0;

// Symbol resolvers from Resolver.c
void* MemLLVM_GetApiAddress(uint32_t hash);
void* MemLLVM_GetApiAddressByName(const char* name);

typedef int (*PFN_CxxFrameHandler)(void*, void*, void*, void*);
typedef int (__stdcall *PFN_RtlAddFunctionTable)(void*, uint32_t, uintptr_t);
typedef int (__stdcall *PFN_RtlDeleteFunctionTable)(void*);
typedef void (__stdcall *PFN_RaiseException)(uint32_t, uint32_t, uint32_t, const uintptr_t*);
typedef void* (*PFN_LoadLibraryA)(const char*);

struct MEMORY_BASIC_INFORMATION_LOCAL {
    void*    BaseAddress;
    void*    AllocationBase;
    uint32_t AllocationProtect;
    uint16_t PartitionId;
    size_t   RegionSize;
    uint32_t State;
    uint32_t Protect;
    uint32_t Type;
};
typedef size_t (__stdcall *PFN_VirtualQuery)(const void*, void*, size_t);

static PFN_CxxFrameHandler s_pfnCxx3 = nullptr;
static PFN_CxxFrameHandler s_pfnCxx4 = nullptr;
static PFN_CxxFrameHandler s_pfnCSpec = nullptr;
static bool s_eh_registered = false;

static void EnsureMsvcrt() {
    PFN_LoadLibraryA pLoad = (PFN_LoadLibraryA)MemLLVM_GetApiAddressByName("LoadLibraryA");
    if (pLoad) {
        pLoad("msvcrt.dll");
    }
}

// Automatically called at startup via __memllvm_dispatch_tu_inits
void MemLLVM_InitExceptions(void) {
    if (s_eh_registered) return;
    s_eh_registered = true;

    if (g_memllvm_pdata_info.pdata_count > 0 && g_memllvm_pdata_info.pdata_ptr) {
        PFN_RtlAddFunctionTable pAdd = (PFN_RtlAddFunctionTable)MemLLVM_GetApiAddressByName("RtlAddFunctionTable");
        if (pAdd) {
            uintptr_t base = (uintptr_t)g_memllvm_pdata_info.image_base;
            pAdd(g_memllvm_pdata_info.pdata_ptr, g_memllvm_pdata_info.pdata_count, base);
        }
    }
}

// Clean up function table on process / module exit
void MemLLVM_CleanupExceptions(void) {
    if (s_eh_registered && g_memllvm_pdata_info.pdata_ptr) {
        PFN_RtlDeleteFunctionTable pDel = (PFN_RtlDeleteFunctionTable)MemLLVM_GetApiAddressByName("RtlDeleteFunctionTable");
        if (pDel) {
            pDel(g_memllvm_pdata_info.pdata_ptr);
        }
        s_eh_registered = false;
    }
}

// Register MemLLVM_InitExceptions into .rdata$mlie section
typedef void (*__memllvm_init_fn_t)(void);
#pragma section(".rdata$mlie", read)
__declspec(allocate(".rdata$mlie")) const __memllvm_init_fn_t __memllvm_init_eh_ptr = MemLLVM_InitExceptions;

static uintptr_t ResolveImageBase(const void* ptr) {
    if (g_memllvm_pdata_info.image_base) {
        return (uintptr_t)g_memllvm_pdata_info.image_base;
    }
    if (g_memllvm_image_base) {
        return g_memllvm_image_base;
    }
    PFN_VirtualQuery pVQ = (PFN_VirtualQuery)MemLLVM_GetApiAddressByName("VirtualQuery");
    if (pVQ) {
        MEMORY_BASIC_INFORMATION_LOCAL mbi = { 0 };
        if (pVQ(ptr, &mbi, sizeof(mbi))) {
            return (uintptr_t)mbi.AllocationBase;
        }
    }
    return 0;
}

//
// Personality Routines forwarding
//

int __CxxFrameHandler3(void* ExceptionRecord, void* EstablisherFrame, void* ContextRecord, void* DispatcherContext) {
    MemLLVM_InitExceptions();
    if (!s_pfnCxx3) {
        s_pfnCxx3 = (PFN_CxxFrameHandler)MemLLVM_GetApiAddressByName("__CxxFrameHandler3");
        if (!s_pfnCxx3) {
            EnsureMsvcrt();
            s_pfnCxx3 = (PFN_CxxFrameHandler)MemLLVM_GetApiAddressByName("__CxxFrameHandler3");
        }
    }
    if (s_pfnCxx3) {
        return s_pfnCxx3(ExceptionRecord, EstablisherFrame, ContextRecord, DispatcherContext);
    }
    return 1; // ExceptionContinueSearch
}

int __CxxFrameHandler4(void* ExceptionRecord, void* EstablisherFrame, void* ContextRecord, void* DispatcherContext) {
    MemLLVM_InitExceptions();
    if (!s_pfnCxx4) {
        s_pfnCxx4 = (PFN_CxxFrameHandler)MemLLVM_GetApiAddressByName("__CxxFrameHandler4");
        if (!s_pfnCxx4) {
            EnsureMsvcrt();
            s_pfnCxx4 = (PFN_CxxFrameHandler)MemLLVM_GetApiAddressByName("__CxxFrameHandler4");
        }
    }
    if (s_pfnCxx4) {
        return s_pfnCxx4(ExceptionRecord, EstablisherFrame, ContextRecord, DispatcherContext);
    }
    return 1; // ExceptionContinueSearch
}

int __C_specific_handler(void* ExceptionRecord, void* EstablisherFrame, void* ContextRecord, void* DispatcherContext) {
    MemLLVM_InitExceptions();
    if (!s_pfnCSpec) {
        s_pfnCSpec = (PFN_CxxFrameHandler)MemLLVM_GetApiAddressByName("__C_specific_handler");
        if (!s_pfnCSpec) {
            EnsureMsvcrt();
            s_pfnCSpec = (PFN_CxxFrameHandler)MemLLVM_GetApiAddressByName("__C_specific_handler");
        }
    }
    if (s_pfnCSpec) {
        return s_pfnCSpec(ExceptionRecord, EstablisherFrame, ContextRecord, DispatcherContext);
    }
    return 1; // ExceptionContinueSearch
}

//
// C++ Exception Thrower
//
void __stdcall _CxxThrowException(void* pExceptionObject, void* pThrowInfo) {
    MemLLVM_InitExceptions();
    PFN_RaiseException pRaise = (PFN_RaiseException)MemLLVM_GetApiAddressByName("RaiseException");
    if (!pRaise) return;

    if (!pExceptionObject) {
        // Rethrow current active exception
        pRaise(0xE06D7363, 1 /* EXCEPTION_NONCONTINUABLE */, 0, nullptr);
        return;
    }

    uintptr_t imgBase = ResolveImageBase(pThrowInfo ? pThrowInfo : pExceptionObject);

    uintptr_t params[4];
    params[0] = 0x19930520; // EH_MAGIC_NUMBER1
    params[1] = (uintptr_t)pExceptionObject;
    params[2] = (uintptr_t)pThrowInfo;
    params[3] = imgBase;

    pRaise(0xE06D7363, 1 /* EXCEPTION_NONCONTINUABLE */, 4, params);
}

} // extern "C"
