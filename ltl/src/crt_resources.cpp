//
// crt_resources.cpp -- MemLLVM Native PE Resource (.rsrc) Subsystem & Win32 Shims
//
// Implements transparent in-memory resource parsing for flat binaries:
// - Parses the 3-level IMAGE_RESOURCE_DIRECTORY tree (Type -> Name/ID -> Language)
// - Supports FindResource(Ex), LoadResource, LockResource, SizeofResource
// - Supports LoadStringA/W, DialogBoxParamA/W, CreateDialogParamA/W, LoadMenuA/W,
//   LoadAcceleratorsA/W, LoadIconA/W, LoadCursorA/W, LoadBitmapA/W, and EnumResource*
// - Transparent fallback to OS APIs for external modules or system resources (e.g. IDI_APPLICATION).
//

#include <windows.h>
#include <stdint.h>
#include <stddef.h>

extern "C" {

#define MEMLLVM_RSRC_MAGIC 0x4D454D4C4C564D52ULL // "MEMLLVMR"

struct MemLLVM_ResourceInfo {
    uint64_t magic;          // 'MEMLLVMR'
    void*    image_base;     // Flat image runtime base (current_base - base_rva)
    void*    rsrc_ptr;       // Pointer to root IMAGE_RESOURCE_DIRECTORY (.rsrc start)
    uint32_t rsrc_size;      // Size of .rsrc section
    uint32_t base_rva;       // base_rva of the flat binary
};

// Global metadata descriptor populated by extract_flat_bin.py's bootstrap stub
__declspec(dllexport) volatile MemLLVM_ResourceInfo g_memllvm_resource_info = {
    MEMLLVM_RSRC_MAGIC,
    nullptr,
    nullptr,
    0,
    0
};
#pragma comment(linker, "/include:g_memllvm_resource_info")

// External API Resolver from Resolver.c
void* MemLLVM_GetApiAddress(uint32_t ror13_hash);

// ROR13 Hashes for Win32 OS Fallbacks
#define HASH_FINDRESOURCEEXW             0xF7E696A5U
#define HASH_FINDRESOURCEEXA             0xF7E6968FU
#define HASH_LOADRESOURCE                0x934E1F7BU
#define HASH_LOCKRESOURCE                0x9A4E2F7BU
#define HASH_SIZEOFRESOURCE              0x3F2A9609U
#define HASH_ENUMRESOURCETYPESW          0xE18D1AC4U
#define HASH_ENUMRESOURCENAMESW          0xDB8B9AC1U
#define HASH_ENUMRESOURCELANGUAGESW      0x8B4CC771U
#define HASH_ENUMRESOURCETYPESA          0xE18D1AAEU
#define HASH_ENUMRESOURCENAMESA          0xDB8B9AABU
#define HASH_ENUMRESOURCELANGUAGESA      0x8B4CC75BU
#define HASH_LOADSTRINGW                 0x378EC0A5U
#define HASH_LOADSTRINGA                 0x378EC08FU
#define HASH_DIALOGBOXINDIRECTPARAMW     0xBFC30876U
#define HASH_DIALOGBOXINDIRECTPARAMA     0xBFC30860U
#define HASH_DIALOGBOXPARAMW             0x20AA84CCU
#define HASH_DIALOGBOXPARAMA             0x20AA84B6U
#define HASH_CREATEDIALOGINDIRECTPARAMW  0x231266BDU
#define HASH_CREATEDIALOGINDIRECTPARAMA  0x231266A7U
#define HASH_CREATEDIALOGPARAMW          0x7008CB2FU
#define HASH_CREATEDIALOGPARAMA          0x7008CB19U
#define HASH_LOADMENUW                   0x1B30F9EAU
#define HASH_LOADMENUA                   0x1B30F9D4U
#define HASH_LOADMENUINDIRECTW           0x2B527806U
#define HASH_LOADMENUINDIRECTA           0x2B5277F0U
#define HASH_LOADACCELERATORSW           0xA27E19DFU
#define HASH_LOADACCELERATORSA           0xA27E19C9U
#define HASH_CREATEACCELERATORTABLEW     0xBDBF9A85U
#define HASH_CREATEACCELERATORTABLEA     0xBDBF9A6FU
#define HASH_LOADICONW                   0x16F8BA2AU
#define HASH_LOADICONA                   0x16F8BA14U
#define HASH_LOOKUPICONIDFROMDIRECTORYEX 0x454EBCEDU
#define HASH_CREATEICONFROMRESOURCEEX    0x250403B3U
#define HASH_LOADCURSORW                 0xCBA6C0E5U
#define HASH_LOADCURSORA                 0xCBA6C0CFU
#define HASH_LOADBITMAPW                 0xBF92DD5FU
#define HASH_LOADBITMAPA                 0xBF92DD49U
#define HASH_CREATEDIBITMAP              0xB3805B8AU
#define HASH_GETMODULEHANDLEW            0xD332491AU

static inline bool IsOurModule(HMODULE hModule) {
    if (!hModule) return true;
    if (hModule == (HMODULE)g_memllvm_resource_info.image_base) return true;
    typedef HMODULE (WINAPI *PFN_GetModuleHandleW)(LPCWSTR);
    static PFN_GetModuleHandleW s_pfnGmh = nullptr;
    if (!s_pfnGmh) s_pfnGmh = (PFN_GetModuleHandleW)MemLLVM_GetApiAddress(HASH_GETMODULEHANDLEW);
    if (s_pfnGmh && hModule == s_pfnGmh(nullptr)) return true;
    return false;
}

static inline bool IsOurResource(const void* ptr) {
    uintptr_t p = (uintptr_t)ptr;
    uintptr_t start = (uintptr_t)g_memllvm_resource_info.rsrc_ptr;
    uintptr_t end = start + g_memllvm_resource_info.rsrc_size;
    return (start != 0 && p >= start && p < end);
}

static bool MatchResourceNameOrIdW(const IMAGE_RESOURCE_DIRECTORY_ENTRY* entry, const uint8_t* rsrc_base, LPCWSTR nameOrId) {
    if (IS_INTRESOURCE(nameOrId)) {
        if (entry->NameIsString) return false;
        return entry->Id == (WORD)(uintptr_t)nameOrId;
    } else {
        if (!entry->NameIsString) return false;
        const IMAGE_RESOURCE_DIRECTORY_STRING* resStr = 
            (const IMAGE_RESOURCE_DIRECTORY_STRING*)(rsrc_base + entry->NameOffset);
        size_t nameLen = 0;
        while (nameOrId[nameLen]) ++nameLen;
        if (resStr->Length != nameLen) return false;
        for (size_t i = 0; i < nameLen; ++i) {
            WCHAR c1 = nameOrId[i];
            WCHAR c2 = resStr->NameString[i];
            if (c1 >= L'A' && c1 <= L'Z') c1 += (L'a' - L'A');
            if (c2 >= L'A' && c2 <= L'Z') c2 += (L'a' - L'A');
            if (c1 != c2) return false;
        }
        return true;
    }
}

static bool MatchResourceNameOrIdA(const IMAGE_RESOURCE_DIRECTORY_ENTRY* entry, const uint8_t* rsrc_base, LPCSTR nameOrId) {
    if (IS_INTRESOURCE(nameOrId)) {
        if (entry->NameIsString) return false;
        return entry->Id == (WORD)(uintptr_t)nameOrId;
    } else {
        if (!entry->NameIsString) return false;
        const IMAGE_RESOURCE_DIRECTORY_STRING* resStr = 
            (const IMAGE_RESOURCE_DIRECTORY_STRING*)(rsrc_base + entry->NameOffset);
        size_t nameLen = 0;
        while (nameOrId[nameLen]) ++nameLen;
        if (resStr->Length != nameLen) return false;
        for (size_t i = 0; i < nameLen; ++i) {
            char c1 = nameOrId[i];
            WCHAR c2 = resStr->NameString[i];
            if (c1 >= 'A' && c1 <= 'Z') c1 += ('a' - 'A');
            if (c2 >= L'A' && c2 <= L'Z') c2 += (L'a' - L'A');
            if ((WCHAR)(unsigned char)c1 != c2) return false;
        }
        return true;
    }
}

static const IMAGE_RESOURCE_DATA_ENTRY* FindResourceInternalW(LPCWSTR lpType, LPCWSTR lpName, WORD wLanguage) {
    const uint8_t* rsrc_base = (const uint8_t*)g_memllvm_resource_info.rsrc_ptr;
    if (!rsrc_base || !g_memllvm_resource_info.rsrc_size) return nullptr;

    const IMAGE_RESOURCE_DIRECTORY* rootDir = (const IMAGE_RESOURCE_DIRECTORY*)rsrc_base;
    uint32_t totalType = rootDir->NumberOfNamedEntries + rootDir->NumberOfIdEntries;
    const IMAGE_RESOURCE_DIRECTORY_ENTRY* typeEntries = (const IMAGE_RESOURCE_DIRECTORY_ENTRY*)(rootDir + 1);

    for (uint32_t i = 0; i < totalType; ++i) {
        if (!MatchResourceNameOrIdW(&typeEntries[i], rsrc_base, lpType))
            continue;
        if (!typeEntries[i].DataIsDirectory)
            continue;

        const IMAGE_RESOURCE_DIRECTORY* nameDir = 
            (const IMAGE_RESOURCE_DIRECTORY*)(rsrc_base + typeEntries[i].OffsetToDirectory);
        uint32_t totalName = nameDir->NumberOfNamedEntries + nameDir->NumberOfIdEntries;
        const IMAGE_RESOURCE_DIRECTORY_ENTRY* nameEntries = (const IMAGE_RESOURCE_DIRECTORY_ENTRY*)(nameDir + 1);

        for (uint32_t j = 0; j < totalName; ++j) {
            if (!MatchResourceNameOrIdW(&nameEntries[j], rsrc_base, lpName))
                continue;
            
            if (nameEntries[j].DataIsDirectory) {
                const IMAGE_RESOURCE_DIRECTORY* langDir = 
                    (const IMAGE_RESOURCE_DIRECTORY*)(rsrc_base + nameEntries[j].OffsetToDirectory);
                uint32_t totalLang = langDir->NumberOfNamedEntries + langDir->NumberOfIdEntries;
                const IMAGE_RESOURCE_DIRECTORY_ENTRY* langEntries = (const IMAGE_RESOURCE_DIRECTORY_ENTRY*)(langDir + 1);
                if (totalLang == 0) continue;

                const IMAGE_RESOURCE_DATA_ENTRY* matchedData = nullptr;
                for (uint32_t k = 0; k < totalLang; ++k) {
                    if (langEntries[k].DataIsDirectory) continue;
                    const IMAGE_RESOURCE_DATA_ENTRY* data = 
                        (const IMAGE_RESOURCE_DATA_ENTRY*)(rsrc_base + langEntries[k].OffsetToData);
                    if (wLanguage == 0 || langEntries[k].Id == wLanguage) {
                        return data;
                    }
                    if (!matchedData) matchedData = data;
                }
                if (matchedData) return matchedData;
            } else {
                return (const IMAGE_RESOURCE_DATA_ENTRY*)(rsrc_base + nameEntries[j].OffsetToData);
            }
        }
    }
    return nullptr;
}

static const IMAGE_RESOURCE_DATA_ENTRY* FindResourceInternalA(LPCSTR lpType, LPCSTR lpName, WORD wLanguage) {
    const uint8_t* rsrc_base = (const uint8_t*)g_memllvm_resource_info.rsrc_ptr;
    if (!rsrc_base || !g_memllvm_resource_info.rsrc_size) return nullptr;

    const IMAGE_RESOURCE_DIRECTORY* rootDir = (const IMAGE_RESOURCE_DIRECTORY*)rsrc_base;
    uint32_t totalType = rootDir->NumberOfNamedEntries + rootDir->NumberOfIdEntries;
    const IMAGE_RESOURCE_DIRECTORY_ENTRY* typeEntries = (const IMAGE_RESOURCE_DIRECTORY_ENTRY*)(rootDir + 1);

    for (uint32_t i = 0; i < totalType; ++i) {
        if (!MatchResourceNameOrIdA(&typeEntries[i], rsrc_base, lpType))
            continue;
        if (!typeEntries[i].DataIsDirectory)
            continue;

        const IMAGE_RESOURCE_DIRECTORY* nameDir = 
            (const IMAGE_RESOURCE_DIRECTORY*)(rsrc_base + typeEntries[i].OffsetToDirectory);
        uint32_t totalName = nameDir->NumberOfNamedEntries + nameDir->NumberOfIdEntries;
        const IMAGE_RESOURCE_DIRECTORY_ENTRY* nameEntries = (const IMAGE_RESOURCE_DIRECTORY_ENTRY*)(nameDir + 1);

        for (uint32_t j = 0; j < totalName; ++j) {
            if (!MatchResourceNameOrIdA(&nameEntries[j], rsrc_base, lpName))
                continue;
            
            if (nameEntries[j].DataIsDirectory) {
                const IMAGE_RESOURCE_DIRECTORY* langDir = 
                    (const IMAGE_RESOURCE_DIRECTORY*)(rsrc_base + nameEntries[j].OffsetToDirectory);
                uint32_t totalLang = langDir->NumberOfNamedEntries + langDir->NumberOfIdEntries;
                const IMAGE_RESOURCE_DIRECTORY_ENTRY* langEntries = (const IMAGE_RESOURCE_DIRECTORY_ENTRY*)(langDir + 1);
                if (totalLang == 0) continue;

                const IMAGE_RESOURCE_DATA_ENTRY* matchedData = nullptr;
                for (uint32_t k = 0; k < totalLang; ++k) {
                    if (langEntries[k].DataIsDirectory) continue;
                    const IMAGE_RESOURCE_DATA_ENTRY* data = 
                        (const IMAGE_RESOURCE_DATA_ENTRY*)(rsrc_base + langEntries[k].OffsetToData);
                    if (wLanguage == 0 || langEntries[k].Id == wLanguage) {
                        return data;
                    }
                    if (!matchedData) matchedData = data;
                }
                if (matchedData) return matchedData;
            } else {
                return (const IMAGE_RESOURCE_DATA_ENTRY*)(rsrc_base + nameEntries[j].OffsetToData);
            }
        }
    }
    return nullptr;
}

//
// Core Win32 Resource API Shims
//

HRSRC WINAPI FindResourceExW(HMODULE hModule, LPCWSTR lpType, LPCWSTR lpName, WORD wLanguage) {
    if (IsOurModule(hModule)) {
        const IMAGE_RESOURCE_DATA_ENTRY* entry = FindResourceInternalW(lpType, lpName, wLanguage);
        if (entry) return (HRSRC)entry;
    }
    typedef HRSRC (WINAPI *PFN_FindResourceExW)(HMODULE, LPCWSTR, LPCWSTR, WORD);
    static PFN_FindResourceExW s_pfnFindExW = nullptr;
    if (!s_pfnFindExW) s_pfnFindExW = (PFN_FindResourceExW)MemLLVM_GetApiAddress(HASH_FINDRESOURCEEXW);
    return s_pfnFindExW ? s_pfnFindExW(hModule, lpType, lpName, wLanguage) : nullptr;
}

HRSRC WINAPI FindResourceW(HMODULE hModule, LPCWSTR lpName, LPCWSTR lpType) {
    return FindResourceExW(hModule, lpType, lpName, 0);
}

HRSRC WINAPI FindResourceExA(HMODULE hModule, LPCSTR lpType, LPCSTR lpName, WORD wLanguage) {
    if (IsOurModule(hModule)) {
        const IMAGE_RESOURCE_DATA_ENTRY* entry = FindResourceInternalA(lpType, lpName, wLanguage);
        if (entry) return (HRSRC)entry;
    }
    typedef HRSRC (WINAPI *PFN_FindResourceExA)(HMODULE, LPCSTR, LPCSTR, WORD);
    static PFN_FindResourceExA s_pfnFindExA = nullptr;
    if (!s_pfnFindExA) s_pfnFindExA = (PFN_FindResourceExA)MemLLVM_GetApiAddress(HASH_FINDRESOURCEEXA);
    return s_pfnFindExA ? s_pfnFindExA(hModule, lpType, lpName, wLanguage) : nullptr;
}

HRSRC WINAPI FindResourceA(HMODULE hModule, LPCSTR lpName, LPCSTR lpType) {
    return FindResourceExA(hModule, lpType, lpName, 0);
}

HGLOBAL WINAPI LoadResource(HMODULE hModule, HRSRC hResInfo) {
    if (IsOurResource(hResInfo)) {
        return (HGLOBAL)hResInfo;
    }
    typedef HGLOBAL (WINAPI *PFN_LoadResource)(HMODULE, HRSRC);
    static PFN_LoadResource s_pfnLoadRes = nullptr;
    if (!s_pfnLoadRes) s_pfnLoadRes = (PFN_LoadResource)MemLLVM_GetApiAddress(HASH_LOADRESOURCE);
    return s_pfnLoadRes ? s_pfnLoadRes(hModule, hResInfo) : nullptr;
}

LPVOID WINAPI LockResource(HGLOBAL hResData) {
    if (IsOurResource(hResData)) {
        const IMAGE_RESOURCE_DATA_ENTRY* data = (const IMAGE_RESOURCE_DATA_ENTRY*)hResData;
        uint8_t* base = (uint8_t*)g_memllvm_resource_info.image_base;
        return base ? (LPVOID)(base + data->OffsetToData) : nullptr;
    }
    typedef LPVOID (WINAPI *PFN_LockResource)(HGLOBAL);
    static PFN_LockResource s_pfnLockRes = nullptr;
    if (!s_pfnLockRes) s_pfnLockRes = (PFN_LockResource)MemLLVM_GetApiAddress(HASH_LOCKRESOURCE);
    return s_pfnLockRes ? s_pfnLockRes(hResData) : nullptr;
}

DWORD WINAPI SizeofResource(HMODULE hModule, HRSRC hResInfo) {
    if (IsOurResource(hResInfo)) {
        const IMAGE_RESOURCE_DATA_ENTRY* data = (const IMAGE_RESOURCE_DATA_ENTRY*)hResInfo;
        return data->Size;
    }
    typedef DWORD (WINAPI *PFN_SizeofResource)(HMODULE, HRSRC);
    static PFN_SizeofResource s_pfnSizeofRes = nullptr;
    if (!s_pfnSizeofRes) s_pfnSizeofRes = (PFN_SizeofResource)MemLLVM_GetApiAddress(HASH_SIZEOFRESOURCE);
    return s_pfnSizeofRes ? s_pfnSizeofRes(hModule, hResInfo) : 0;
}

//
// String Resources (STRINGTABLE)
//

int WINAPI LoadStringW(HINSTANCE hInstance, UINT uID, LPWSTR lpBuffer, int cchBufferMax) {
    if (IsOurModule(hInstance)) {
        UINT blockId = (uID / 16) + 1;
        UINT stringIdx = uID % 16;
        HRSRC hRes = FindResourceExW(hInstance, (LPCWSTR)RT_STRING, MAKEINTRESOURCEW(blockId), 0);
        if (hRes) {
            const WCHAR* p = (const WCHAR*)LockResource(LoadResource(hInstance, hRes));
            if (p) {
                for (UINT i = 0; i < stringIdx; ++i) {
                    WORD len = *p++;
                    p += len;
                }
                WORD len = *p++;
                if (cchBufferMax == 0) {
                    *(const WCHAR**)lpBuffer = p;
                    return (int)len;
                }
                int toCopy = (len < (WORD)(cchBufferMax - 1)) ? len : (cchBufferMax - 1);
                if (toCopy > 0) {
                    memcpy(lpBuffer, p, toCopy * sizeof(WCHAR));
                }
                lpBuffer[toCopy] = L'\0';
                return toCopy;
            }
        }
    }
    typedef int (WINAPI *PFN_LoadStringW)(HINSTANCE, UINT, LPWSTR, int);
    static PFN_LoadStringW s_pfnLoadStringW = nullptr;
    if (!s_pfnLoadStringW) s_pfnLoadStringW = (PFN_LoadStringW)MemLLVM_GetApiAddress(HASH_LOADSTRINGW);
    return s_pfnLoadStringW ? s_pfnLoadStringW(hInstance, uID, lpBuffer, cchBufferMax) : 0;
}

int WINAPI LoadStringA(HINSTANCE hInstance, UINT uID, LPSTR lpBuffer, int cchBufferMax) {
    if (cchBufferMax <= 0 || !lpBuffer) return 0;
    WCHAR wbuf[1024];
    int wlen = LoadStringW(hInstance, uID, wbuf, 1024);
    if (wlen > 0) {
        int toCopy = (wlen < cchBufferMax - 1) ? wlen : (cchBufferMax - 1);
        for (int i = 0; i < toCopy; ++i) {
            lpBuffer[i] = (wbuf[i] < 128) ? (char)wbuf[i] : '?';
        }
        lpBuffer[toCopy] = '\0';
        return toCopy;
    }
    typedef int (WINAPI *PFN_LoadStringA)(HINSTANCE, UINT, LPSTR, int);
    static PFN_LoadStringA s_pfnLoadStringA = nullptr;
    if (!s_pfnLoadStringA) s_pfnLoadStringA = (PFN_LoadStringA)MemLLVM_GetApiAddress(HASH_LOADSTRINGA);
    return s_pfnLoadStringA ? s_pfnLoadStringA(hInstance, uID, lpBuffer, cchBufferMax) : 0;
}

//
// Dialog Resources
//

INT_PTR WINAPI DialogBoxParamW(HINSTANCE hInstance, LPCWSTR lpTemplateName, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam) {
    if (IsOurModule(hInstance)) {
        HRSRC hRes = FindResourceExW(hInstance, (LPCWSTR)RT_DIALOG, lpTemplateName, 0);
        if (hRes) {
            LPCDLGTEMPLATEW pTemplate = (LPCDLGTEMPLATEW)LockResource(LoadResource(hInstance, hRes));
            if (pTemplate) {
                typedef INT_PTR (WINAPI *PFN_DialogBoxIndirectParamW)(HINSTANCE, LPCDLGTEMPLATEW, HWND, DLGPROC, LPARAM);
                static PFN_DialogBoxIndirectParamW s_pfnDbipW = nullptr;
                if (!s_pfnDbipW) s_pfnDbipW = (PFN_DialogBoxIndirectParamW)MemLLVM_GetApiAddress(HASH_DIALOGBOXINDIRECTPARAMW);
                if (s_pfnDbipW) return s_pfnDbipW(hInstance, pTemplate, hWndParent, lpDialogFunc, dwInitParam);
            }
        }
    }
    typedef INT_PTR (WINAPI *PFN_DialogBoxParamW)(HINSTANCE, LPCWSTR, HWND, DLGPROC, LPARAM);
    static PFN_DialogBoxParamW s_pfnDbpW = nullptr;
    if (!s_pfnDbpW) s_pfnDbpW = (PFN_DialogBoxParamW)MemLLVM_GetApiAddress(HASH_DIALOGBOXPARAMW);
    return s_pfnDbpW ? s_pfnDbpW(hInstance, lpTemplateName, hWndParent, lpDialogFunc, dwInitParam) : -1;
}

INT_PTR WINAPI DialogBoxParamA(HINSTANCE hInstance, LPCSTR lpTemplateName, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam) {
    if (IsOurModule(hInstance)) {
        HRSRC hRes = FindResourceExA(hInstance, (LPCSTR)RT_DIALOG, lpTemplateName, 0);
        if (hRes) {
            LPCDLGTEMPLATEA pTemplate = (LPCDLGTEMPLATEA)LockResource(LoadResource(hInstance, hRes));
            if (pTemplate) {
                typedef INT_PTR (WINAPI *PFN_DialogBoxIndirectParamA)(HINSTANCE, LPCDLGTEMPLATEA, HWND, DLGPROC, LPARAM);
                static PFN_DialogBoxIndirectParamA s_pfnDbipA = nullptr;
                if (!s_pfnDbipA) s_pfnDbipA = (PFN_DialogBoxIndirectParamA)MemLLVM_GetApiAddress(HASH_DIALOGBOXINDIRECTPARAMA);
                if (s_pfnDbipA) return s_pfnDbipA(hInstance, pTemplate, hWndParent, lpDialogFunc, dwInitParam);
            }
        }
    }
    typedef INT_PTR (WINAPI *PFN_DialogBoxParamA)(HINSTANCE, LPCSTR, HWND, DLGPROC, LPARAM);
    static PFN_DialogBoxParamA s_pfnDbpA = nullptr;
    if (!s_pfnDbpA) s_pfnDbpA = (PFN_DialogBoxParamA)MemLLVM_GetApiAddress(HASH_DIALOGBOXPARAMA);
    return s_pfnDbpA ? s_pfnDbpA(hInstance, lpTemplateName, hWndParent, lpDialogFunc, dwInitParam) : -1;
}

HWND WINAPI CreateDialogParamW(HINSTANCE hInstance, LPCWSTR lpTemplateName, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam) {
    if (IsOurModule(hInstance)) {
        HRSRC hRes = FindResourceExW(hInstance, (LPCWSTR)RT_DIALOG, lpTemplateName, 0);
        if (hRes) {
            LPCDLGTEMPLATEW pTemplate = (LPCDLGTEMPLATEW)LockResource(LoadResource(hInstance, hRes));
            if (pTemplate) {
                typedef HWND (WINAPI *PFN_CreateDialogIndirectParamW)(HINSTANCE, LPCDLGTEMPLATEW, HWND, DLGPROC, LPARAM);
                static PFN_CreateDialogIndirectParamW s_pfnCdipW = nullptr;
                if (!s_pfnCdipW) s_pfnCdipW = (PFN_CreateDialogIndirectParamW)MemLLVM_GetApiAddress(HASH_CREATEDIALOGINDIRECTPARAMW);
                if (s_pfnCdipW) return s_pfnCdipW(hInstance, pTemplate, hWndParent, lpDialogFunc, dwInitParam);
            }
        }
    }
    typedef HWND (WINAPI *PFN_CreateDialogParamW)(HINSTANCE, LPCWSTR, HWND, DLGPROC, LPARAM);
    static PFN_CreateDialogParamW s_pfnCdpW = nullptr;
    if (!s_pfnCdpW) s_pfnCdpW = (PFN_CreateDialogParamW)MemLLVM_GetApiAddress(HASH_CREATEDIALOGPARAMW);
    return s_pfnCdpW ? s_pfnCdpW(hInstance, lpTemplateName, hWndParent, lpDialogFunc, dwInitParam) : nullptr;
}

HWND WINAPI CreateDialogParamA(HINSTANCE hInstance, LPCSTR lpTemplateName, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam) {
    if (IsOurModule(hInstance)) {
        HRSRC hRes = FindResourceExA(hInstance, (LPCSTR)RT_DIALOG, lpTemplateName, 0);
        if (hRes) {
            LPCDLGTEMPLATEA pTemplate = (LPCDLGTEMPLATEA)LockResource(LoadResource(hInstance, hRes));
            if (pTemplate) {
                typedef HWND (WINAPI *PFN_CreateDialogIndirectParamA)(HINSTANCE, LPCDLGTEMPLATEA, HWND, DLGPROC, LPARAM);
                static PFN_CreateDialogIndirectParamA s_pfnCdipA = nullptr;
                if (!s_pfnCdipA) s_pfnCdipA = (PFN_CreateDialogIndirectParamA)MemLLVM_GetApiAddress(HASH_CREATEDIALOGINDIRECTPARAMA);
                if (s_pfnCdipA) return s_pfnCdipA(hInstance, pTemplate, hWndParent, lpDialogFunc, dwInitParam);
            }
        }
    }
    typedef HWND (WINAPI *PFN_CreateDialogParamA)(HINSTANCE, LPCSTR, HWND, DLGPROC, LPARAM);
    static PFN_CreateDialogParamA s_pfnCdpA = nullptr;
    if (!s_pfnCdpA) s_pfnCdpA = (PFN_CreateDialogParamA)MemLLVM_GetApiAddress(HASH_CREATEDIALOGPARAMA);
    return s_pfnCdpA ? s_pfnCdpA(hInstance, lpTemplateName, hWndParent, lpDialogFunc, dwInitParam) : nullptr;
}

//
// Menus & Accelerators
//

HMENU WINAPI LoadMenuW(HINSTANCE hInstance, LPCWSTR lpMenuName) {
    if (IsOurModule(hInstance)) {
        HRSRC hRes = FindResourceExW(hInstance, (LPCWSTR)RT_MENU, lpMenuName, 0);
        if (hRes) {
            const MENUTEMPLATE* pTemplate = (const MENUTEMPLATE*)LockResource(LoadResource(hInstance, hRes));
            if (pTemplate) {
                typedef HMENU (WINAPI *PFN_LoadMenuIndirectW)(const MENUTEMPLATE*);
                static PFN_LoadMenuIndirectW s_pfnLmiW = nullptr;
                if (!s_pfnLmiW) s_pfnLmiW = (PFN_LoadMenuIndirectW)MemLLVM_GetApiAddress(HASH_LOADMENUINDIRECTW);
                if (s_pfnLmiW) return s_pfnLmiW(pTemplate);
            }
        }
    }
    typedef HMENU (WINAPI *PFN_LoadMenuW)(HINSTANCE, LPCWSTR);
    static PFN_LoadMenuW s_pfnLmW = nullptr;
    if (!s_pfnLmW) s_pfnLmW = (PFN_LoadMenuW)MemLLVM_GetApiAddress(HASH_LOADMENUW);
    return s_pfnLmW ? s_pfnLmW(hInstance, lpMenuName) : nullptr;
}

HMENU WINAPI LoadMenuA(HINSTANCE hInstance, LPCSTR lpMenuName) {
    if (IsOurModule(hInstance)) {
        HRSRC hRes = FindResourceExA(hInstance, (LPCSTR)RT_MENU, lpMenuName, 0);
        if (hRes) {
            const MENUTEMPLATE* pTemplate = (const MENUTEMPLATE*)LockResource(LoadResource(hInstance, hRes));
            if (pTemplate) {
                typedef HMENU (WINAPI *PFN_LoadMenuIndirectA)(const MENUTEMPLATE*);
                static PFN_LoadMenuIndirectA s_pfnLmiA = nullptr;
                if (!s_pfnLmiA) s_pfnLmiA = (PFN_LoadMenuIndirectA)MemLLVM_GetApiAddress(HASH_LOADMENUINDIRECTA);
                if (s_pfnLmiA) return s_pfnLmiA(pTemplate);
            }
        }
    }
    typedef HMENU (WINAPI *PFN_LoadMenuA)(HINSTANCE, LPCSTR);
    static PFN_LoadMenuA s_pfnLmA = nullptr;
    if (!s_pfnLmA) s_pfnLmA = (PFN_LoadMenuA)MemLLVM_GetApiAddress(HASH_LOADMENUA);
    return s_pfnLmA ? s_pfnLmA(hInstance, lpMenuName) : nullptr;
}

HACCEL WINAPI LoadAcceleratorsW(HINSTANCE hInstance, LPCWSTR lpTableName) {
    if (IsOurModule(hInstance)) {
        HRSRC hRes = FindResourceExW(hInstance, (LPCWSTR)RT_ACCELERATOR, lpTableName, 0);
        if (hRes) {
            const ACCEL* pAccel = (const ACCEL*)LockResource(LoadResource(hInstance, hRes));
            DWORD size = SizeofResource(hInstance, hRes);
            if (pAccel && size >= sizeof(ACCEL)) {
                typedef HACCEL (WINAPI *PFN_CreateAcceleratorTableW)(const ACCEL*, int);
                static PFN_CreateAcceleratorTableW s_pfnCatW = nullptr;
                if (!s_pfnCatW) s_pfnCatW = (PFN_CreateAcceleratorTableW)MemLLVM_GetApiAddress(HASH_CREATEACCELERATORTABLEW);
                if (s_pfnCatW) return s_pfnCatW(pAccel, (int)(size / sizeof(ACCEL)));
            }
        }
    }
    typedef HACCEL (WINAPI *PFN_LoadAcceleratorsW)(HINSTANCE, LPCWSTR);
    static PFN_LoadAcceleratorsW s_pfnLaW = nullptr;
    if (!s_pfnLaW) s_pfnLaW = (PFN_LoadAcceleratorsW)MemLLVM_GetApiAddress(HASH_LOADACCELERATORSW);
    return s_pfnLaW ? s_pfnLaW(hInstance, lpTableName) : nullptr;
}

HACCEL WINAPI LoadAcceleratorsA(HINSTANCE hInstance, LPCSTR lpTableName) {
    if (IsOurModule(hInstance)) {
        HRSRC hRes = FindResourceExA(hInstance, (LPCSTR)RT_ACCELERATOR, lpTableName, 0);
        if (hRes) {
            const ACCEL* pAccel = (const ACCEL*)LockResource(LoadResource(hInstance, hRes));
            DWORD size = SizeofResource(hInstance, hRes);
            if (pAccel && size >= sizeof(ACCEL)) {
                typedef HACCEL (WINAPI *PFN_CreateAcceleratorTableA)(const ACCEL*, int);
                static PFN_CreateAcceleratorTableA s_pfnCatA = nullptr;
                if (!s_pfnCatA) s_pfnCatA = (PFN_CreateAcceleratorTableA)MemLLVM_GetApiAddress(HASH_CREATEACCELERATORTABLEA);
                if (s_pfnCatA) return s_pfnCatA(pAccel, (int)(size / sizeof(ACCEL)));
            }
        }
    }
    typedef HACCEL (WINAPI *PFN_LoadAcceleratorsA)(HINSTANCE, LPCSTR);
    static PFN_LoadAcceleratorsA s_pfnLaA = nullptr;
    if (!s_pfnLaA) s_pfnLaA = (PFN_LoadAcceleratorsA)MemLLVM_GetApiAddress(HASH_LOADACCELERATORSA);
    return s_pfnLaA ? s_pfnLaA(hInstance, lpTableName) : nullptr;
}

//
// Icons, Cursors & Bitmaps
//

HICON WINAPI LoadIconW(HINSTANCE hInstance, LPCWSTR lpIconName) {
    if (IsOurModule(hInstance)) {
        HRSRC hRes = FindResourceExW(hInstance, (LPCWSTR)RT_GROUP_ICON, lpIconName, 0);
        if (hRes) {
            PBYTE pGrp = (PBYTE)LockResource(LoadResource(hInstance, hRes));
            if (pGrp) {
                typedef int (WINAPI *PFN_LookupIconIdFromDirectoryEx)(PBYTE, BOOL, int, int, UINT);
                static PFN_LookupIconIdFromDirectoryEx s_pfnLookup = nullptr;
                if (!s_pfnLookup) s_pfnLookup = (PFN_LookupIconIdFromDirectoryEx)MemLLVM_GetApiAddress(HASH_LOOKUPICONIDFROMDIRECTORYEX);
                
                int iconId = s_pfnLookup ? s_pfnLookup(pGrp, TRUE, 0, 0, LR_DEFAULTCOLOR) : 0;
                if (iconId > 0) {
                    HRSRC hIconRes = FindResourceExW(hInstance, (LPCWSTR)RT_ICON, MAKEINTRESOURCEW(iconId), 0);
                    if (hIconRes) {
                        PBYTE pIconBits = (PBYTE)LockResource(LoadResource(hInstance, hIconRes));
                        DWORD iconSize = SizeofResource(hInstance, hIconRes);
                        if (pIconBits && iconSize > 0) {
                            typedef HICON (WINAPI *PFN_CreateIconFromResourceEx)(PBYTE, DWORD, BOOL, DWORD, int, int, UINT);
                            static PFN_CreateIconFromResourceEx s_pfnCreate = nullptr;
                            if (!s_pfnCreate) s_pfnCreate = (PFN_CreateIconFromResourceEx)MemLLVM_GetApiAddress(HASH_CREATEICONFROMRESOURCEEX);
                            if (s_pfnCreate) return s_pfnCreate(pIconBits, iconSize, TRUE, 0x00030000, 0, 0, LR_DEFAULTCOLOR);
                        }
                    }
                }
            }
        }
    }
    typedef HICON (WINAPI *PFN_LoadIconW)(HINSTANCE, LPCWSTR);
    static PFN_LoadIconW s_pfnLoadIconW = nullptr;
    if (!s_pfnLoadIconW) s_pfnLoadIconW = (PFN_LoadIconW)MemLLVM_GetApiAddress(HASH_LOADICONW);
    return s_pfnLoadIconW ? s_pfnLoadIconW(hInstance, lpIconName) : nullptr;
}

HICON WINAPI LoadIconA(HINSTANCE hInstance, LPCSTR lpIconName) {
    if (IsOurModule(hInstance)) {
        HRSRC hRes = FindResourceExA(hInstance, (LPCSTR)RT_GROUP_ICON, lpIconName, 0);
        if (hRes) {
            PBYTE pGrp = (PBYTE)LockResource(LoadResource(hInstance, hRes));
            if (pGrp) {
                typedef int (WINAPI *PFN_LookupIconIdFromDirectoryEx)(PBYTE, BOOL, int, int, UINT);
                static PFN_LookupIconIdFromDirectoryEx s_pfnLookup = nullptr;
                if (!s_pfnLookup) s_pfnLookup = (PFN_LookupIconIdFromDirectoryEx)MemLLVM_GetApiAddress(HASH_LOOKUPICONIDFROMDIRECTORYEX);
                
                int iconId = s_pfnLookup ? s_pfnLookup(pGrp, TRUE, 0, 0, LR_DEFAULTCOLOR) : 0;
                if (iconId > 0) {
                    HRSRC hIconRes = FindResourceExW(hInstance, (LPCWSTR)RT_ICON, MAKEINTRESOURCEW(iconId), 0);
                    if (hIconRes) {
                        PBYTE pIconBits = (PBYTE)LockResource(LoadResource(hInstance, hIconRes));
                        DWORD iconSize = SizeofResource(hInstance, hIconRes);
                        if (pIconBits && iconSize > 0) {
                            typedef HICON (WINAPI *PFN_CreateIconFromResourceEx)(PBYTE, DWORD, BOOL, DWORD, int, int, UINT);
                            static PFN_CreateIconFromResourceEx s_pfnCreate = nullptr;
                            if (!s_pfnCreate) s_pfnCreate = (PFN_CreateIconFromResourceEx)MemLLVM_GetApiAddress(HASH_CREATEICONFROMRESOURCEEX);
                            if (s_pfnCreate) return s_pfnCreate(pIconBits, iconSize, TRUE, 0x00030000, 0, 0, LR_DEFAULTCOLOR);
                        }
                    }
                }
            }
        }
    }
    typedef HICON (WINAPI *PFN_LoadIconA)(HINSTANCE, LPCSTR);
    static PFN_LoadIconA s_pfnLoadIconA = nullptr;
    if (!s_pfnLoadIconA) s_pfnLoadIconA = (PFN_LoadIconA)MemLLVM_GetApiAddress(HASH_LOADICONA);
    return s_pfnLoadIconA ? s_pfnLoadIconA(hInstance, lpIconName) : nullptr;
}

HCURSOR WINAPI LoadCursorW(HINSTANCE hInstance, LPCWSTR lpCursorName) {
    if (IsOurModule(hInstance)) {
        HRSRC hRes = FindResourceExW(hInstance, (LPCWSTR)RT_GROUP_CURSOR, lpCursorName, 0);
        if (hRes) {
            PBYTE pGrp = (PBYTE)LockResource(LoadResource(hInstance, hRes));
            if (pGrp) {
                typedef int (WINAPI *PFN_LookupIconIdFromDirectoryEx)(PBYTE, BOOL, int, int, UINT);
                static PFN_LookupIconIdFromDirectoryEx s_pfnLookup = nullptr;
                if (!s_pfnLookup) s_pfnLookup = (PFN_LookupIconIdFromDirectoryEx)MemLLVM_GetApiAddress(HASH_LOOKUPICONIDFROMDIRECTORYEX);
                
                int cursorId = s_pfnLookup ? s_pfnLookup(pGrp, FALSE, 0, 0, LR_DEFAULTCOLOR) : 0;
                if (cursorId > 0) {
                    HRSRC hCurRes = FindResourceExW(hInstance, (LPCWSTR)RT_CURSOR, MAKEINTRESOURCEW(cursorId), 0);
                    if (hCurRes) {
                        PBYTE pCurBits = (PBYTE)LockResource(LoadResource(hInstance, hCurRes));
                        DWORD curSize = SizeofResource(hInstance, hCurRes);
                        if (pCurBits && curSize > 0) {
                            typedef HICON (WINAPI *PFN_CreateIconFromResourceEx)(PBYTE, DWORD, BOOL, DWORD, int, int, UINT);
                            static PFN_CreateIconFromResourceEx s_pfnCreate = nullptr;
                            if (!s_pfnCreate) s_pfnCreate = (PFN_CreateIconFromResourceEx)MemLLVM_GetApiAddress(HASH_CREATEICONFROMRESOURCEEX);
                            if (s_pfnCreate) return s_pfnCreate(pCurBits, curSize, FALSE, 0x00030000, 0, 0, LR_DEFAULTCOLOR);
                        }
                    }
                }
            }
        }
    }
    typedef HCURSOR (WINAPI *PFN_LoadCursorW)(HINSTANCE, LPCWSTR);
    static PFN_LoadCursorW s_pfnLoadCursorW = nullptr;
    if (!s_pfnLoadCursorW) s_pfnLoadCursorW = (PFN_LoadCursorW)MemLLVM_GetApiAddress(HASH_LOADCURSORW);
    return s_pfnLoadCursorW ? s_pfnLoadCursorW(hInstance, lpCursorName) : nullptr;
}

HCURSOR WINAPI LoadCursorA(HINSTANCE hInstance, LPCSTR lpCursorName) {
    if (IsOurModule(hInstance)) {
        HRSRC hRes = FindResourceExA(hInstance, (LPCSTR)RT_GROUP_CURSOR, lpCursorName, 0);
        if (hRes) {
            PBYTE pGrp = (PBYTE)LockResource(LoadResource(hInstance, hRes));
            if (pGrp) {
                typedef int (WINAPI *PFN_LookupIconIdFromDirectoryEx)(PBYTE, BOOL, int, int, UINT);
                static PFN_LookupIconIdFromDirectoryEx s_pfnLookup = nullptr;
                if (!s_pfnLookup) s_pfnLookup = (PFN_LookupIconIdFromDirectoryEx)MemLLVM_GetApiAddress(HASH_LOOKUPICONIDFROMDIRECTORYEX);
                
                int cursorId = s_pfnLookup ? s_pfnLookup(pGrp, FALSE, 0, 0, LR_DEFAULTCOLOR) : 0;
                if (cursorId > 0) {
                    HRSRC hCurRes = FindResourceExW(hInstance, (LPCWSTR)RT_CURSOR, MAKEINTRESOURCEW(cursorId), 0);
                    if (hCurRes) {
                        PBYTE pCurBits = (PBYTE)LockResource(LoadResource(hInstance, hCurRes));
                        DWORD curSize = SizeofResource(hInstance, hCurRes);
                        if (pCurBits && curSize > 0) {
                            typedef HICON (WINAPI *PFN_CreateIconFromResourceEx)(PBYTE, DWORD, BOOL, DWORD, int, int, UINT);
                            static PFN_CreateIconFromResourceEx s_pfnCreate = nullptr;
                            if (!s_pfnCreate) s_pfnCreate = (PFN_CreateIconFromResourceEx)MemLLVM_GetApiAddress(HASH_CREATEICONFROMRESOURCEEX);
                            if (s_pfnCreate) return s_pfnCreate(pCurBits, curSize, FALSE, 0x00030000, 0, 0, LR_DEFAULTCOLOR);
                        }
                    }
                }
            }
        }
    }
    typedef HCURSOR (WINAPI *PFN_LoadCursorA)(HINSTANCE, LPCSTR);
    static PFN_LoadCursorA s_pfnLoadCursorA = nullptr;
    if (!s_pfnLoadCursorA) s_pfnLoadCursorA = (PFN_LoadCursorA)MemLLVM_GetApiAddress(HASH_LOADCURSORA);
    return s_pfnLoadCursorA ? s_pfnLoadCursorA(hInstance, lpCursorName) : nullptr;
}

HBITMAP WINAPI LoadBitmapW(HINSTANCE hInstance, LPCWSTR lpBitmapName) {
    if (IsOurModule(hInstance)) {
        HRSRC hRes = FindResourceExW(hInstance, (LPCWSTR)RT_BITMAP, lpBitmapName, 0);
        if (hRes) {
            const BITMAPINFOHEADER* pbmih = (const BITMAPINFOHEADER*)LockResource(LoadResource(hInstance, hRes));
            if (pbmih) {
                typedef HBITMAP (WINAPI *PFN_CreateDIBitmap)(HDC, const BITMAPINFOHEADER*, DWORD, const void*, const BITMAPINFO*, UINT);
                static PFN_CreateDIBitmap s_pfnCdib = nullptr;
                if (!s_pfnCdib) s_pfnCdib = (PFN_CreateDIBitmap)MemLLVM_GetApiAddress(HASH_CREATEDIBITMAP);
                if (s_pfnCdib) {
                    DWORD colorTableSize = 0;
                    if (pbmih->biBitCount <= 8) {
                        colorTableSize = (pbmih->biClrUsed ? pbmih->biClrUsed : (1 << pbmih->biBitCount)) * sizeof(RGBQUAD);
                    }
                    const void* bits = (const uint8_t*)pbmih + pbmih->biSize + colorTableSize;
                    return s_pfnCdib(nullptr, pbmih, CBM_INIT, bits, (const BITMAPINFO*)pbmih, DIB_RGB_COLORS);
                }
            }
        }
    }
    typedef HBITMAP (WINAPI *PFN_LoadBitmapW)(HINSTANCE, LPCWSTR);
    static PFN_LoadBitmapW s_pfnLoadBitmapW = nullptr;
    if (!s_pfnLoadBitmapW) s_pfnLoadBitmapW = (PFN_LoadBitmapW)MemLLVM_GetApiAddress(HASH_LOADBITMAPW);
    return s_pfnLoadBitmapW ? s_pfnLoadBitmapW(hInstance, lpBitmapName) : nullptr;
}

HBITMAP WINAPI LoadBitmapA(HINSTANCE hInstance, LPCSTR lpBitmapName) {
    if (IsOurModule(hInstance)) {
        HRSRC hRes = FindResourceExA(hInstance, (LPCSTR)RT_BITMAP, lpBitmapName, 0);
        if (hRes) {
            const BITMAPINFOHEADER* pbmih = (const BITMAPINFOHEADER*)LockResource(LoadResource(hInstance, hRes));
            if (pbmih) {
                typedef HBITMAP (WINAPI *PFN_CreateDIBitmap)(HDC, const BITMAPINFOHEADER*, DWORD, const void*, const BITMAPINFO*, UINT);
                static PFN_CreateDIBitmap s_pfnCdib = nullptr;
                if (!s_pfnCdib) s_pfnCdib = (PFN_CreateDIBitmap)MemLLVM_GetApiAddress(HASH_CREATEDIBITMAP);
                if (s_pfnCdib) {
                    DWORD colorTableSize = 0;
                    if (pbmih->biBitCount <= 8) {
                        colorTableSize = (pbmih->biClrUsed ? pbmih->biClrUsed : (1 << pbmih->biBitCount)) * sizeof(RGBQUAD);
                    }
                    const void* bits = (const uint8_t*)pbmih + pbmih->biSize + colorTableSize;
                    return s_pfnCdib(nullptr, pbmih, CBM_INIT, bits, (const BITMAPINFO*)pbmih, DIB_RGB_COLORS);
                }
            }
        }
    }
    typedef HBITMAP (WINAPI *PFN_LoadBitmapA)(HINSTANCE, LPCSTR);
    static PFN_LoadBitmapA s_pfnLoadBitmapA = nullptr;
    if (!s_pfnLoadBitmapA) s_pfnLoadBitmapA = (PFN_LoadBitmapA)MemLLVM_GetApiAddress(HASH_LOADBITMAPA);
    return s_pfnLoadBitmapA ? s_pfnLoadBitmapA(hInstance, lpBitmapName) : nullptr;
}

//
// Resource Enumeration
//

BOOL WINAPI EnumResourceTypesW(HMODULE hModule, ENUMRESTYPEPROCW lpEnumFunc, LONG_PTR lParam) {
    if (IsOurModule(hModule) && g_memllvm_resource_info.rsrc_ptr) {
        const uint8_t* rsrc_base = (const uint8_t*)g_memllvm_resource_info.rsrc_ptr;
        const IMAGE_RESOURCE_DIRECTORY* rootDir = (const IMAGE_RESOURCE_DIRECTORY*)rsrc_base;
        uint32_t totalType = rootDir->NumberOfNamedEntries + rootDir->NumberOfIdEntries;
        const IMAGE_RESOURCE_DIRECTORY_ENTRY* entries = (const IMAGE_RESOURCE_DIRECTORY_ENTRY*)(rootDir + 1);
        for (uint32_t i = 0; i < totalType; ++i) {
            LPWSTR typeName = nullptr;
            WCHAR nameBuf[256];
            if (entries[i].NameIsString) {
                const IMAGE_RESOURCE_DIRECTORY_STRING* resStr = 
                    (const IMAGE_RESOURCE_DIRECTORY_STRING*)(rsrc_base + entries[i].NameOffset);
                int copyLen = (resStr->Length < 255) ? resStr->Length : 255;
                memcpy(nameBuf, resStr->NameString, copyLen * sizeof(WCHAR));
                nameBuf[copyLen] = L'\0';
                typeName = nameBuf;
            } else {
                typeName = MAKEINTRESOURCEW(entries[i].Id);
            }
            if (!lpEnumFunc(hModule, typeName, lParam)) return FALSE;
        }
        return TRUE;
    }
    typedef BOOL (WINAPI *PFN_EnumResourceTypesW)(HMODULE, ENUMRESTYPEPROCW, LONG_PTR);
    static PFN_EnumResourceTypesW s_pfnEnumTypesW = nullptr;
    if (!s_pfnEnumTypesW) s_pfnEnumTypesW = (PFN_EnumResourceTypesW)MemLLVM_GetApiAddress(HASH_ENUMRESOURCETYPESW);
    return s_pfnEnumTypesW ? s_pfnEnumTypesW(hModule, lpEnumFunc, lParam) : FALSE;
}

BOOL WINAPI EnumResourceNamesW(HMODULE hModule, LPCWSTR lpType, ENUMRESNAMEPROCW lpEnumFunc, LONG_PTR lParam) {
    if (IsOurModule(hModule) && g_memllvm_resource_info.rsrc_ptr) {
        const uint8_t* rsrc_base = (const uint8_t*)g_memllvm_resource_info.rsrc_ptr;
        const IMAGE_RESOURCE_DIRECTORY* rootDir = (const IMAGE_RESOURCE_DIRECTORY*)rsrc_base;
        uint32_t totalType = rootDir->NumberOfNamedEntries + rootDir->NumberOfIdEntries;
        const IMAGE_RESOURCE_DIRECTORY_ENTRY* typeEntries = (const IMAGE_RESOURCE_DIRECTORY_ENTRY*)(rootDir + 1);
        for (uint32_t i = 0; i < totalType; ++i) {
            if (!MatchResourceNameOrIdW(&typeEntries[i], rsrc_base, lpType)) continue;
            if (!typeEntries[i].DataIsDirectory) continue;

            const IMAGE_RESOURCE_DIRECTORY* nameDir = 
                (const IMAGE_RESOURCE_DIRECTORY*)(rsrc_base + typeEntries[i].OffsetToDirectory);
            uint32_t totalName = nameDir->NumberOfNamedEntries + nameDir->NumberOfIdEntries;
            const IMAGE_RESOURCE_DIRECTORY_ENTRY* nameEntries = (const IMAGE_RESOURCE_DIRECTORY_ENTRY*)(nameDir + 1);
            for (uint32_t j = 0; j < totalName; ++j) {
                LPWSTR resName = nullptr;
                WCHAR nameBuf[256];
                if (nameEntries[j].NameIsString) {
                    const IMAGE_RESOURCE_DIRECTORY_STRING* resStr = 
                        (const IMAGE_RESOURCE_DIRECTORY_STRING*)(rsrc_base + nameEntries[j].NameOffset);
                    int copyLen = (resStr->Length < 255) ? resStr->Length : 255;
                    memcpy(nameBuf, resStr->NameString, copyLen * sizeof(WCHAR));
                    nameBuf[copyLen] = L'\0';
                    resName = nameBuf;
                } else {
                    resName = MAKEINTRESOURCEW(nameEntries[j].Id);
                }
                if (!lpEnumFunc(hModule, lpType, resName, lParam)) return FALSE;
            }
            return TRUE;
        }
        return FALSE;
    }
    typedef BOOL (WINAPI *PFN_EnumResourceNamesW)(HMODULE, LPCWSTR, ENUMRESNAMEPROCW, LONG_PTR);
    static PFN_EnumResourceNamesW s_pfnEnumNamesW = nullptr;
    if (!s_pfnEnumNamesW) s_pfnEnumNamesW = (PFN_EnumResourceNamesW)MemLLVM_GetApiAddress(HASH_ENUMRESOURCENAMESW);
    return s_pfnEnumNamesW ? s_pfnEnumNamesW(hModule, lpType, lpEnumFunc, lParam) : FALSE;
}

BOOL WINAPI EnumResourceLanguagesW(HMODULE hModule, LPCWSTR lpType, LPCWSTR lpName, ENUMRESLANGPROCW lpEnumFunc, LONG_PTR lParam) {
    if (IsOurModule(hModule) && g_memllvm_resource_info.rsrc_ptr) {
        const uint8_t* rsrc_base = (const uint8_t*)g_memllvm_resource_info.rsrc_ptr;
        const IMAGE_RESOURCE_DIRECTORY* rootDir = (const IMAGE_RESOURCE_DIRECTORY*)rsrc_base;
        uint32_t totalType = rootDir->NumberOfNamedEntries + rootDir->NumberOfIdEntries;
        const IMAGE_RESOURCE_DIRECTORY_ENTRY* typeEntries = (const IMAGE_RESOURCE_DIRECTORY_ENTRY*)(rootDir + 1);
        for (uint32_t i = 0; i < totalType; ++i) {
            if (!MatchResourceNameOrIdW(&typeEntries[i], rsrc_base, lpType)) continue;
            if (!typeEntries[i].DataIsDirectory) continue;

            const IMAGE_RESOURCE_DIRECTORY* nameDir = 
                (const IMAGE_RESOURCE_DIRECTORY*)(rsrc_base + typeEntries[i].OffsetToDirectory);
            uint32_t totalName = nameDir->NumberOfNamedEntries + nameDir->NumberOfIdEntries;
            const IMAGE_RESOURCE_DIRECTORY_ENTRY* nameEntries = (const IMAGE_RESOURCE_DIRECTORY_ENTRY*)(nameDir + 1);
            for (uint32_t j = 0; j < totalName; ++j) {
                if (!MatchResourceNameOrIdW(&nameEntries[j], rsrc_base, lpName)) continue;
                if (!nameEntries[j].DataIsDirectory) continue;

                const IMAGE_RESOURCE_DIRECTORY* langDir = 
                    (const IMAGE_RESOURCE_DIRECTORY*)(rsrc_base + nameEntries[j].OffsetToDirectory);
                uint32_t totalLang = langDir->NumberOfNamedEntries + langDir->NumberOfIdEntries;
                const IMAGE_RESOURCE_DIRECTORY_ENTRY* langEntries = (const IMAGE_RESOURCE_DIRECTORY_ENTRY*)(langDir + 1);
                for (uint32_t k = 0; k < totalLang; ++k) {
                    if (!lpEnumFunc(hModule, lpType, lpName, langEntries[k].Id, lParam)) return FALSE;
                }
                return TRUE;
            }
        }
        return FALSE;
    }
    typedef BOOL (WINAPI *PFN_EnumResourceLanguagesW)(HMODULE, LPCWSTR, LPCWSTR, ENUMRESLANGPROCW, LONG_PTR);
    static PFN_EnumResourceLanguagesW s_pfnEnumLangW = nullptr;
    if (!s_pfnEnumLangW) s_pfnEnumLangW = (PFN_EnumResourceLanguagesW)MemLLVM_GetApiAddress(HASH_ENUMRESOURCELANGUAGESW);
    return s_pfnEnumLangW ? s_pfnEnumLangW(hModule, lpType, lpName, lpEnumFunc, lParam) : FALSE;
}

// __imp_* aliases to satisfy any dllimport loads
void* const __imp_FindResourceW = (void*)FindResourceW;
void* const __imp_FindResourceA = (void*)FindResourceA;
void* const __imp_FindResourceExW = (void*)FindResourceExW;
void* const __imp_FindResourceExA = (void*)FindResourceExA;
void* const __imp_LoadResource = (void*)LoadResource;
void* const __imp_LockResource = (void*)LockResource;
void* const __imp_SizeofResource = (void*)SizeofResource;
void* const __imp_LoadStringW = (void*)LoadStringW;
void* const __imp_LoadStringA = (void*)LoadStringA;
void* const __imp_DialogBoxParamW = (void*)DialogBoxParamW;
void* const __imp_DialogBoxParamA = (void*)DialogBoxParamA;
void* const __imp_CreateDialogParamW = (void*)CreateDialogParamW;
void* const __imp_CreateDialogParamA = (void*)CreateDialogParamA;
void* const __imp_LoadMenuW = (void*)LoadMenuW;
void* const __imp_LoadMenuA = (void*)LoadMenuA;
void* const __imp_LoadAcceleratorsW = (void*)LoadAcceleratorsW;
void* const __imp_LoadAcceleratorsA = (void*)LoadAcceleratorsA;
void* const __imp_LoadIconW = (void*)LoadIconW;
void* const __imp_LoadIconA = (void*)LoadIconA;
void* const __imp_LoadCursorW = (void*)LoadCursorW;
void* const __imp_LoadCursorA = (void*)LoadCursorA;
void* const __imp_LoadBitmapW = (void*)LoadBitmapW;
void* const __imp_LoadBitmapA = (void*)LoadBitmapA;
void* const __imp_EnumResourceTypesW = (void*)EnumResourceTypesW;
void* const __imp_EnumResourceNamesW = (void*)EnumResourceNamesW;
void* const __imp_EnumResourceLanguagesW = (void*)EnumResourceLanguagesW;

} // extern "C"
