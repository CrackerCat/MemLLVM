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

#define MEMLLVM_RSRC_MAGIC 0x5D7B2E94A1C8F036ULL // Non-ASCII pseudo-random token

struct MemLLVM_ResourceInfo {
    uint64_t magic;          // 'MEMLLVMR'
    void*    image_base;     // Flat image runtime base (current_base - base_rva)
    void*    rsrc_ptr;       // Pointer to root IMAGE_RESOURCE_DIRECTORY (.rsrc start)
    uint32_t rsrc_size;      // Size of .rsrc section
    uint32_t base_rva;       // base_rva of the flat binary
};

// Global metadata descriptor populated by extract_flat_bin.py's bootstrap stub
volatile MemLLVM_ResourceInfo g_memllvm_resource_info = {
    MEMLLVM_RSRC_MAGIC,
    nullptr,
    nullptr,
    0,
    0
};
#pragma comment(linker, "/include:g_memllvm_resource_info")

// External API Resolver from Resolver.c
void* MemLLVM_GetApiAddress(uint32_t hash);
void* MemLLVM_GetApiAddressByName(const char* name);

static inline bool IsOurModule(HMODULE hModule) {
    if (!hModule) return true;
    if (hModule == (HMODULE)g_memllvm_resource_info.image_base) return true;
    typedef HMODULE (WINAPI *PFN_GetModuleHandleW)(LPCWSTR);
    static PFN_GetModuleHandleW s_pfnGmh = nullptr;
    if (!s_pfnGmh) s_pfnGmh = (PFN_GetModuleHandleW)MemLLVM_GetApiAddressByName("GetModuleHandleW");
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
    if (!s_pfnFindExW) s_pfnFindExW = (PFN_FindResourceExW)MemLLVM_GetApiAddressByName("FindResourceExW");
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
    if (!s_pfnFindExA) s_pfnFindExA = (PFN_FindResourceExA)MemLLVM_GetApiAddressByName("FindResourceExA");
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
    if (!s_pfnLoadRes) s_pfnLoadRes = (PFN_LoadResource)MemLLVM_GetApiAddressByName("LoadResource");
    return s_pfnLoadRes ? s_pfnLoadRes(hModule, hResInfo) : nullptr;
}

//
// Leaf Decryption and Ephemeral Wipe Subsystem
//

static inline void DecryptResourcePayload(const uint8_t* src, uint8_t* dst, uint32_t size, uint32_t key, uint32_t rva) {
    uint32_t state = (uint32_t)(key ^ (rva * 0x9E3779B9u) ^ (size * 0x85EBCA6Bu));
    for (uint32_t i = 0; i < size; ++i) {
        state = state * 1664525u + 1013904223u;
        dst[i] = src[i] ^ (uint8_t)(state >> 24);
    }
}

struct EphemeralResourceBuffer {
    uint8_t* ptr;
    uint32_t size;
    bool     is_heap;
    uint8_t  stack_buf[2048];

    EphemeralResourceBuffer(const IMAGE_RESOURCE_DATA_ENTRY* data) : ptr(nullptr), size(0), is_heap(false) {
        if (!data || !g_memllvm_resource_info.image_base) return;
        size = data->Size;
        if (size == 0) return;

        const uint8_t* enc_src = (const uint8_t*)g_memllvm_resource_info.image_base + data->OffsetToData;
        if (data->Reserved == 0) {
            ptr = (uint8_t*)enc_src;
            return;
        }

        if (size <= sizeof(stack_buf)) {
            ptr = stack_buf;
            is_heap = false;
        } else {
            typedef HANDLE (WINAPI *PFN_GetProcessHeap)();
            typedef LPVOID (WINAPI *PFN_HeapAlloc)(HANDLE, DWORD, SIZE_T);
            static PFN_GetProcessHeap s_pfnGph = nullptr;
            static PFN_HeapAlloc s_pfnHa = nullptr;
            if (!s_pfnGph) s_pfnGph = (PFN_GetProcessHeap)MemLLVM_GetApiAddressByName("GetProcessHeap");
            if (!s_pfnHa) s_pfnHa = (PFN_HeapAlloc)MemLLVM_GetApiAddressByName("HeapAlloc");
            HANDLE hHeap = s_pfnGph ? s_pfnGph() : nullptr;
            ptr = (hHeap && s_pfnHa) ? (uint8_t*)s_pfnHa(hHeap, 0, size) : nullptr;
            is_heap = true;
        }

        if (ptr) {
            DecryptResourcePayload(enc_src, ptr, size, data->Reserved, data->OffsetToData);
        }
    }

    ~EphemeralResourceBuffer() {
        Wipe();
    }

    void Wipe() {
        if (ptr && is_heap) {
            volatile uint8_t* vptr = (volatile uint8_t*)ptr;
            for (uint32_t i = 0; i < size; ++i) vptr[i] = 0;

            typedef HANDLE (WINAPI *PFN_GetProcessHeap)();
            typedef BOOL (WINAPI *PFN_HeapFree)(HANDLE, DWORD, LPVOID);
            static PFN_GetProcessHeap s_pfnGph = nullptr;
            static PFN_HeapFree s_pfnHf = nullptr;
            if (!s_pfnGph) s_pfnGph = (PFN_GetProcessHeap)MemLLVM_GetApiAddressByName("GetProcessHeap");
            if (!s_pfnHf) s_pfnHf = (PFN_HeapFree)MemLLVM_GetApiAddressByName("HeapFree");
            HANDLE hHeap = s_pfnGph ? s_pfnGph() : nullptr;
            if (hHeap && s_pfnHf) s_pfnHf(hHeap, 0, ptr);
            ptr = nullptr;
        } else if (ptr && !is_heap && ptr == stack_buf) {
            volatile uint8_t* vptr = (volatile uint8_t*)ptr;
            for (uint32_t i = 0; i < size; ++i) vptr[i] = 0;
            ptr = nullptr;
        }
    }
};

struct EphemeralSlot {
    uint8_t* buf;
    uint32_t capacity;
    uint32_t active_size;
    uint32_t last_rva;
};
static EphemeralSlot s_ring[4] = {};
static uint32_t s_ring_idx = 0;

static LPVOID GetEphemeralResource(const IMAGE_RESOURCE_DATA_ENTRY* data) {
    if (!data || !g_memllvm_resource_info.image_base) return nullptr;
    if (data->Reserved == 0) {
        return (uint8_t*)g_memllvm_resource_info.image_base + data->OffsetToData;
    }

    for (int i = 0; i < 4; ++i) {
        if (s_ring[i].buf && s_ring[i].last_rva == data->OffsetToData && s_ring[i].active_size == data->Size) {
            return s_ring[i].buf;
        }
    }

    uint32_t slot = s_ring_idx;
    s_ring_idx = (s_ring_idx + 1) % 4;

    if (s_ring[slot].buf && s_ring[slot].active_size > 0) {
        volatile uint8_t* vptr = (volatile uint8_t*)s_ring[slot].buf;
        for (uint32_t i = 0; i < s_ring[slot].active_size; ++i) vptr[i] = 0;
        s_ring[slot].active_size = 0;
        s_ring[slot].last_rva = 0;
    }

    typedef HANDLE (WINAPI *PFN_GetProcessHeap)();
    typedef LPVOID (WINAPI *PFN_HeapAlloc)(HANDLE, DWORD, SIZE_T);
    typedef LPVOID (WINAPI *PFN_HeapReAlloc)(HANDLE, DWORD, LPVOID, SIZE_T);
    static PFN_GetProcessHeap s_pfnGph = nullptr;
    static PFN_HeapAlloc s_pfnHa = nullptr;
    static PFN_HeapReAlloc s_pfnHra = nullptr;
    if (!s_pfnGph) s_pfnGph = (PFN_GetProcessHeap)MemLLVM_GetApiAddressByName("GetProcessHeap");
    if (!s_pfnHa) s_pfnHa = (PFN_HeapAlloc)MemLLVM_GetApiAddressByName("HeapAlloc");
    if (!s_pfnHra) s_pfnHra = (PFN_HeapReAlloc)MemLLVM_GetApiAddressByName("HeapReAlloc");
    HANDLE hHeap = s_pfnGph ? s_pfnGph() : nullptr;

    if (data->Size > s_ring[slot].capacity) {
        if (s_ring[slot].buf) {
            s_ring[slot].buf = (uint8_t*)s_pfnHra(hHeap, 0, s_ring[slot].buf, data->Size);
        } else {
            s_ring[slot].buf = (uint8_t*)s_pfnHa(hHeap, 0, data->Size);
        }
        s_ring[slot].capacity = s_ring[slot].buf ? data->Size : 0;
    }

    if (!s_ring[slot].buf) return nullptr;

    const uint8_t* enc_src = (const uint8_t*)g_memllvm_resource_info.image_base + data->OffsetToData;
    DecryptResourcePayload(enc_src, s_ring[slot].buf, data->Size, data->Reserved, data->OffsetToData);
    s_ring[slot].active_size = data->Size;
    s_ring[slot].last_rva = data->OffsetToData;

    return s_ring[slot].buf;
}

struct CachedIconEntry {
    bool   isIcon;
    bool   isIntResource;
    WORD   id;
    WCHAR  name[64];
    int    cx;
    int    cy;
    HICON  handle;
};
static CachedIconEntry s_iconCache[32] = {};
static uint32_t s_iconCacheCount = 0;

static HICON FindCachedIcon(bool isIcon, LPCWSTR name, int cx, int cy) {
    bool isInt = IS_INTRESOURCE(name);
    WORD id = isInt ? (WORD)(uintptr_t)name : 0;
    for (uint32_t i = 0; i < s_iconCacheCount; ++i) {
        if (s_iconCache[i].isIcon == isIcon && s_iconCache[i].isIntResource == isInt &&
            s_iconCache[i].cx == cx && s_iconCache[i].cy == cy) {
            if (isInt) {
                if (s_iconCache[i].id == id) return s_iconCache[i].handle;
            } else {
                const WCHAR* a = name;
                const WCHAR* b = s_iconCache[i].name;
                bool match = true;
                while (*a || *b) {
                    if (*a++ != *b++) { match = false; break; }
                }
                if (match) return s_iconCache[i].handle;
            }
        }
    }
    return nullptr;
}

static void AddCachedIcon(bool isIcon, LPCWSTR name, int cx, int cy, HICON h) {
    if (s_iconCacheCount >= 32 || !h) return;
    uint32_t idx = s_iconCacheCount++;
    s_iconCache[idx].isIcon = isIcon;
    s_iconCache[idx].isIntResource = IS_INTRESOURCE(name);
    s_iconCache[idx].cx = cx;
    s_iconCache[idx].cy = cy;
    s_iconCache[idx].handle = h;
    if (s_iconCache[idx].isIntResource) {
        s_iconCache[idx].id = (WORD)(uintptr_t)name;
        s_iconCache[idx].name[0] = L'\0';
    } else {
        s_iconCache[idx].id = 0;
        int i = 0;
        while (name[i] && i < 63) {
            s_iconCache[idx].name[i] = name[i];
            i++;
        }
        s_iconCache[idx].name[i] = L'\0';
    }
}

LPVOID WINAPI LockResource(HGLOBAL hResData) {
    if (IsOurResource(hResData)) {
        const IMAGE_RESOURCE_DATA_ENTRY* data = (const IMAGE_RESOURCE_DATA_ENTRY*)hResData;
        return GetEphemeralResource(data);
    }
    typedef LPVOID (WINAPI *PFN_LockResource)(HGLOBAL);
    static PFN_LockResource s_pfnLockRes = nullptr;
    if (!s_pfnLockRes) s_pfnLockRes = (PFN_LockResource)MemLLVM_GetApiAddressByName("LockResource");
    return s_pfnLockRes ? s_pfnLockRes(hResData) : nullptr;
}

DWORD WINAPI SizeofResource(HMODULE hModule, HRSRC hResInfo) {
    if (IsOurResource(hResInfo)) {
        const IMAGE_RESOURCE_DATA_ENTRY* data = (const IMAGE_RESOURCE_DATA_ENTRY*)hResInfo;
        return data->Size;
    }
    typedef DWORD (WINAPI *PFN_SizeofResource)(HMODULE, HRSRC);
    static PFN_SizeofResource s_pfnSizeofRes = nullptr;
    if (!s_pfnSizeofRes) s_pfnSizeofRes = (PFN_SizeofResource)MemLLVM_GetApiAddressByName("SizeofResource");
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
            const IMAGE_RESOURCE_DATA_ENTRY* data = (const IMAGE_RESOURCE_DATA_ENTRY*)hRes;
            if (cchBufferMax == 0) {
                const WCHAR* p = (const WCHAR*)GetEphemeralResource(data);
                if (p) {
                    for (UINT i = 0; i < stringIdx; ++i) {
                        WORD len = *p++;
                        p += len;
                    }
                    WORD len = *p++;
                    *(const WCHAR**)lpBuffer = p;
                    return (int)len;
                }
            } else {
                EphemeralResourceBuffer dec(data);
                if (dec.ptr) {
                    const WCHAR* p = (const WCHAR*)dec.ptr;
                    for (UINT i = 0; i < stringIdx; ++i) {
                        WORD len = *p++;
                        p += len;
                    }
                    WORD len = *p++;
                    int toCopy = (len < (WORD)(cchBufferMax - 1)) ? len : (cchBufferMax - 1);
                    if (toCopy > 0) {
                        memcpy(lpBuffer, p, toCopy * sizeof(WCHAR));
                    }
                    lpBuffer[toCopy] = L'\0';
                    return toCopy;
                }
            }
        }
    }
    typedef int (WINAPI *PFN_LoadStringW)(HINSTANCE, UINT, LPWSTR, int);
    static PFN_LoadStringW s_pfnLoadStringW = nullptr;
    if (!s_pfnLoadStringW) s_pfnLoadStringW = (PFN_LoadStringW)MemLLVM_GetApiAddressByName("LoadStringW");
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
    if (!s_pfnLoadStringA) s_pfnLoadStringA = (PFN_LoadStringA)MemLLVM_GetApiAddressByName("LoadStringA");
    return s_pfnLoadStringA ? s_pfnLoadStringA(hInstance, uID, lpBuffer, cchBufferMax) : 0;
}

//
// Dialog Resources
//

INT_PTR WINAPI DialogBoxParamW(HINSTANCE hInstance, LPCWSTR lpTemplateName, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam) {
    if (IsOurModule(hInstance)) {
        HRSRC hRes = FindResourceExW(hInstance, (LPCWSTR)RT_DIALOG, lpTemplateName, 0);
        if (hRes) {
            const IMAGE_RESOURCE_DATA_ENTRY* data = (const IMAGE_RESOURCE_DATA_ENTRY*)hRes;
            EphemeralResourceBuffer dec(data);
            if (dec.ptr) {
                typedef INT_PTR (WINAPI *PFN_DialogBoxIndirectParamW)(HINSTANCE, LPCDLGTEMPLATEW, HWND, DLGPROC, LPARAM);
                static PFN_DialogBoxIndirectParamW s_pfnDbipW = nullptr;
                if (!s_pfnDbipW) s_pfnDbipW = (PFN_DialogBoxIndirectParamW)MemLLVM_GetApiAddressByName("DialogBoxIndirectParamW");
                if (s_pfnDbipW) {
                    INT_PTR ret = s_pfnDbipW(hInstance, (LPCDLGTEMPLATEW)dec.ptr, hWndParent, lpDialogFunc, dwInitParam);
                    dec.Wipe();
                    return ret;
                }
            }
        }
    }
    typedef INT_PTR (WINAPI *PFN_DialogBoxParamW)(HINSTANCE, LPCWSTR, HWND, DLGPROC, LPARAM);
    static PFN_DialogBoxParamW s_pfnDbpW = nullptr;
    if (!s_pfnDbpW) s_pfnDbpW = (PFN_DialogBoxParamW)MemLLVM_GetApiAddressByName("DialogBoxParamW");
    return s_pfnDbpW ? s_pfnDbpW(hInstance, lpTemplateName, hWndParent, lpDialogFunc, dwInitParam) : -1;
}

INT_PTR WINAPI DialogBoxParamA(HINSTANCE hInstance, LPCSTR lpTemplateName, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam) {
    if (IsOurModule(hInstance)) {
        HRSRC hRes = FindResourceExA(hInstance, (LPCSTR)RT_DIALOG, lpTemplateName, 0);
        if (hRes) {
            const IMAGE_RESOURCE_DATA_ENTRY* data = (const IMAGE_RESOURCE_DATA_ENTRY*)hRes;
            EphemeralResourceBuffer dec(data);
            if (dec.ptr) {
                typedef INT_PTR (WINAPI *PFN_DialogBoxIndirectParamA)(HINSTANCE, LPCDLGTEMPLATEA, HWND, DLGPROC, LPARAM);
                static PFN_DialogBoxIndirectParamA s_pfnDbipA = nullptr;
                if (!s_pfnDbipA) s_pfnDbipA = (PFN_DialogBoxIndirectParamA)MemLLVM_GetApiAddressByName("DialogBoxIndirectParamA");
                if (s_pfnDbipA) {
                    INT_PTR ret = s_pfnDbipA(hInstance, (LPCDLGTEMPLATEA)dec.ptr, hWndParent, lpDialogFunc, dwInitParam);
                    dec.Wipe();
                    return ret;
                }
            }
        }
    }
    typedef INT_PTR (WINAPI *PFN_DialogBoxParamA)(HINSTANCE, LPCSTR, HWND, DLGPROC, LPARAM);
    static PFN_DialogBoxParamA s_pfnDbpA = nullptr;
    if (!s_pfnDbpA) s_pfnDbpA = (PFN_DialogBoxParamA)MemLLVM_GetApiAddressByName("DialogBoxParamA");
    return s_pfnDbpA ? s_pfnDbpA(hInstance, lpTemplateName, hWndParent, lpDialogFunc, dwInitParam) : -1;
}

HWND WINAPI CreateDialogParamW(HINSTANCE hInstance, LPCWSTR lpTemplateName, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam) {
    if (IsOurModule(hInstance)) {
        HRSRC hRes = FindResourceExW(hInstance, (LPCWSTR)RT_DIALOG, lpTemplateName, 0);
        if (hRes) {
            const IMAGE_RESOURCE_DATA_ENTRY* data = (const IMAGE_RESOURCE_DATA_ENTRY*)hRes;
            EphemeralResourceBuffer dec(data);
            if (dec.ptr) {
                typedef HWND (WINAPI *PFN_CreateDialogIndirectParamW)(HINSTANCE, LPCDLGTEMPLATEW, HWND, DLGPROC, LPARAM);
                static PFN_CreateDialogIndirectParamW s_pfnCdipW = nullptr;
                if (!s_pfnCdipW) s_pfnCdipW = (PFN_CreateDialogIndirectParamW)MemLLVM_GetApiAddressByName("CreateDialogIndirectParamW");
                if (s_pfnCdipW) {
                    HWND hWnd = s_pfnCdipW(hInstance, (LPCDLGTEMPLATEW)dec.ptr, hWndParent, lpDialogFunc, dwInitParam);
                    dec.Wipe();
                    return hWnd;
                }
            }
        }
    }
    typedef HWND (WINAPI *PFN_CreateDialogParamW)(HINSTANCE, LPCWSTR, HWND, DLGPROC, LPARAM);
    static PFN_CreateDialogParamW s_pfnCdpW = nullptr;
    if (!s_pfnCdpW) s_pfnCdpW = (PFN_CreateDialogParamW)MemLLVM_GetApiAddressByName("CreateDialogParamW");
    return s_pfnCdpW ? s_pfnCdpW(hInstance, lpTemplateName, hWndParent, lpDialogFunc, dwInitParam) : nullptr;
}

HWND WINAPI CreateDialogParamA(HINSTANCE hInstance, LPCSTR lpTemplateName, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam) {
    if (IsOurModule(hInstance)) {
        HRSRC hRes = FindResourceExA(hInstance, (LPCSTR)RT_DIALOG, lpTemplateName, 0);
        if (hRes) {
            const IMAGE_RESOURCE_DATA_ENTRY* data = (const IMAGE_RESOURCE_DATA_ENTRY*)hRes;
            EphemeralResourceBuffer dec(data);
            if (dec.ptr) {
                typedef HWND (WINAPI *PFN_CreateDialogIndirectParamA)(HINSTANCE, LPCDLGTEMPLATEA, HWND, DLGPROC, LPARAM);
                static PFN_CreateDialogIndirectParamA s_pfnCdipA = nullptr;
                if (!s_pfnCdipA) s_pfnCdipA = (PFN_CreateDialogIndirectParamA)MemLLVM_GetApiAddressByName("CreateDialogIndirectParamA");
                if (s_pfnCdipA) {
                    HWND hWnd = s_pfnCdipA(hInstance, (LPCDLGTEMPLATEA)dec.ptr, hWndParent, lpDialogFunc, dwInitParam);
                    dec.Wipe();
                    return hWnd;
                }
            }
        }
    }
    typedef HWND (WINAPI *PFN_CreateDialogParamA)(HINSTANCE, LPCSTR, HWND, DLGPROC, LPARAM);
    static PFN_CreateDialogParamA s_pfnCdpA = nullptr;
    if (!s_pfnCdpA) s_pfnCdpA = (PFN_CreateDialogParamA)MemLLVM_GetApiAddressByName("CreateDialogParamA");
    return s_pfnCdpA ? s_pfnCdpA(hInstance, lpTemplateName, hWndParent, lpDialogFunc, dwInitParam) : nullptr;
}

//
// Menus & Accelerators
//

HMENU WINAPI LoadMenuW(HINSTANCE hInstance, LPCWSTR lpMenuName) {
    if (IsOurModule(hInstance)) {
        HRSRC hRes = FindResourceExW(hInstance, (LPCWSTR)RT_MENU, lpMenuName, 0);
        if (hRes) {
            const IMAGE_RESOURCE_DATA_ENTRY* data = (const IMAGE_RESOURCE_DATA_ENTRY*)hRes;
            EphemeralResourceBuffer dec(data);
            if (dec.ptr) {
                typedef HMENU (WINAPI *PFN_LoadMenuIndirectW)(const MENUTEMPLATE*);
                static PFN_LoadMenuIndirectW s_pfnLmiW = nullptr;
                if (!s_pfnLmiW) s_pfnLmiW = (PFN_LoadMenuIndirectW)MemLLVM_GetApiAddressByName("LoadMenuIndirectW");
                if (s_pfnLmiW) {
                    HMENU hMenu = s_pfnLmiW((const MENUTEMPLATE*)dec.ptr);
                    dec.Wipe();
                    return hMenu;
                }
            }
        }
    }
    typedef HMENU (WINAPI *PFN_LoadMenuW)(HINSTANCE, LPCWSTR);
    static PFN_LoadMenuW s_pfnLmW = nullptr;
    if (!s_pfnLmW) s_pfnLmW = (PFN_LoadMenuW)MemLLVM_GetApiAddressByName("LoadMenuW");
    return s_pfnLmW ? s_pfnLmW(hInstance, lpMenuName) : nullptr;
}

HMENU WINAPI LoadMenuA(HINSTANCE hInstance, LPCSTR lpMenuName) {
    if (IsOurModule(hInstance)) {
        HRSRC hRes = FindResourceExA(hInstance, (LPCSTR)RT_MENU, lpMenuName, 0);
        if (hRes) {
            const IMAGE_RESOURCE_DATA_ENTRY* data = (const IMAGE_RESOURCE_DATA_ENTRY*)hRes;
            EphemeralResourceBuffer dec(data);
            if (dec.ptr) {
                typedef HMENU (WINAPI *PFN_LoadMenuIndirectA)(const MENUTEMPLATE*);
                static PFN_LoadMenuIndirectA s_pfnLmiA = nullptr;
                if (!s_pfnLmiA) s_pfnLmiA = (PFN_LoadMenuIndirectA)MemLLVM_GetApiAddressByName("LoadMenuIndirectA");
                if (s_pfnLmiA) {
                    HMENU hMenu = s_pfnLmiA((const MENUTEMPLATE*)dec.ptr);
                    dec.Wipe();
                    return hMenu;
                }
            }
        }
    }
    typedef HMENU (WINAPI *PFN_LoadMenuA)(HINSTANCE, LPCSTR);
    static PFN_LoadMenuA s_pfnLmA = nullptr;
    if (!s_pfnLmA) s_pfnLmA = (PFN_LoadMenuA)MemLLVM_GetApiAddressByName("LoadMenuA");
    return s_pfnLmA ? s_pfnLmA(hInstance, lpMenuName) : nullptr;
}

HACCEL WINAPI LoadAcceleratorsW(HINSTANCE hInstance, LPCWSTR lpTableName) {
    if (IsOurModule(hInstance)) {
        HRSRC hRes = FindResourceExW(hInstance, (LPCWSTR)RT_ACCELERATOR, lpTableName, 0);
        if (hRes) {
            const IMAGE_RESOURCE_DATA_ENTRY* data = (const IMAGE_RESOURCE_DATA_ENTRY*)hRes;
            EphemeralResourceBuffer dec(data);
            if (dec.ptr && dec.size >= sizeof(ACCEL)) {
                typedef HACCEL (WINAPI *PFN_CreateAcceleratorTableW)(const ACCEL*, int);
                static PFN_CreateAcceleratorTableW s_pfnCatW = nullptr;
                if (!s_pfnCatW) s_pfnCatW = (PFN_CreateAcceleratorTableW)MemLLVM_GetApiAddressByName("CreateAcceleratorTableW");
                if (s_pfnCatW) {
                    HACCEL hAccel = s_pfnCatW((const ACCEL*)dec.ptr, (int)(dec.size / sizeof(ACCEL)));
                    dec.Wipe();
                    return hAccel;
                }
            }
        }
    }
    typedef HACCEL (WINAPI *PFN_LoadAcceleratorsW)(HINSTANCE, LPCWSTR);
    static PFN_LoadAcceleratorsW s_pfnLaW = nullptr;
    if (!s_pfnLaW) s_pfnLaW = (PFN_LoadAcceleratorsW)MemLLVM_GetApiAddressByName("LoadAcceleratorsW");
    return s_pfnLaW ? s_pfnLaW(hInstance, lpTableName) : nullptr;
}

HACCEL WINAPI LoadAcceleratorsA(HINSTANCE hInstance, LPCSTR lpTableName) {
    if (IsOurModule(hInstance)) {
        HRSRC hRes = FindResourceExA(hInstance, (LPCSTR)RT_ACCELERATOR, lpTableName, 0);
        if (hRes) {
            const IMAGE_RESOURCE_DATA_ENTRY* data = (const IMAGE_RESOURCE_DATA_ENTRY*)hRes;
            EphemeralResourceBuffer dec(data);
            if (dec.ptr && dec.size >= sizeof(ACCEL)) {
                typedef HACCEL (WINAPI *PFN_CreateAcceleratorTableA)(const ACCEL*, int);
                static PFN_CreateAcceleratorTableA s_pfnCatA = nullptr;
                if (!s_pfnCatA) s_pfnCatA = (PFN_CreateAcceleratorTableA)MemLLVM_GetApiAddressByName("CreateAcceleratorTableA");
                if (s_pfnCatA) {
                    HACCEL hAccel = s_pfnCatA((const ACCEL*)dec.ptr, (int)(dec.size / sizeof(ACCEL)));
                    dec.Wipe();
                    return hAccel;
                }
            }
        }
    }
    typedef HACCEL (WINAPI *PFN_LoadAcceleratorsA)(HINSTANCE, LPCSTR);
    static PFN_LoadAcceleratorsA s_pfnLaA = nullptr;
    if (!s_pfnLaA) s_pfnLaA = (PFN_LoadAcceleratorsA)MemLLVM_GetApiAddressByName("LoadAcceleratorsA");
    return s_pfnLaA ? s_pfnLaA(hInstance, lpTableName) : nullptr;
}

//
// Bitmaps
//

HBITMAP WINAPI LoadBitmapW(HINSTANCE hInstance, LPCWSTR lpBitmapName) {
    if (IsOurModule(hInstance)) {
        HRSRC hRes = FindResourceExW(hInstance, (LPCWSTR)RT_BITMAP, lpBitmapName, 0);
        if (hRes) {
            const IMAGE_RESOURCE_DATA_ENTRY* data = (const IMAGE_RESOURCE_DATA_ENTRY*)hRes;
            EphemeralResourceBuffer dec(data);
            if (dec.ptr) {
                const BITMAPINFOHEADER* pbmih = (const BITMAPINFOHEADER*)dec.ptr;
                typedef HBITMAP (WINAPI *PFN_CreateDIBitmap)(HDC, const BITMAPINFOHEADER*, DWORD, const void*, const BITMAPINFO*, UINT);
                static PFN_CreateDIBitmap s_pfnCdib = nullptr;
                if (!s_pfnCdib) s_pfnCdib = (PFN_CreateDIBitmap)MemLLVM_GetApiAddressByName("CreateDIBitmap");
                if (s_pfnCdib) {
                    DWORD colorTableSize = 0;
                    if (pbmih->biBitCount <= 8) {
                        colorTableSize = (pbmih->biClrUsed ? pbmih->biClrUsed : (1 << pbmih->biBitCount)) * sizeof(RGBQUAD);
                    }
                    const void* bits = (const uint8_t*)pbmih + pbmih->biSize + colorTableSize;
                    typedef HDC (WINAPI *PFN_GetDC)(HWND);
                    typedef int (WINAPI *PFN_ReleaseDC)(HWND, HDC);
                    static PFN_GetDC s_pfnGetDC = nullptr;
                    static PFN_ReleaseDC s_pfnReleaseDC = nullptr;
                    if (!s_pfnGetDC) s_pfnGetDC = (PFN_GetDC)MemLLVM_GetApiAddressByName("GetDC");
                    if (!s_pfnReleaseDC) s_pfnReleaseDC = (PFN_ReleaseDC)MemLLVM_GetApiAddressByName("ReleaseDC");

                    HDC hScreenDC = s_pfnGetDC ? s_pfnGetDC(nullptr) : nullptr;
                    HBITMAP hBmp = s_pfnCdib(hScreenDC, pbmih, CBM_INIT, bits, (const BITMAPINFO*)pbmih, DIB_RGB_COLORS);
                    if (s_pfnReleaseDC && hScreenDC) s_pfnReleaseDC(nullptr, hScreenDC);
                    dec.Wipe();
                    return hBmp;
                }
            }
        }
    }
    typedef HBITMAP (WINAPI *PFN_LoadBitmapW)(HINSTANCE, LPCWSTR);
    static PFN_LoadBitmapW s_pfnLoadBitmapW = nullptr;
    if (!s_pfnLoadBitmapW) s_pfnLoadBitmapW = (PFN_LoadBitmapW)MemLLVM_GetApiAddressByName("LoadBitmapW");
    return s_pfnLoadBitmapW ? s_pfnLoadBitmapW(hInstance, lpBitmapName) : nullptr;
}

HBITMAP WINAPI LoadBitmapA(HINSTANCE hInstance, LPCSTR lpBitmapName) {
    if (IsOurModule(hInstance)) {
        HRSRC hRes = FindResourceExA(hInstance, (LPCSTR)RT_BITMAP, lpBitmapName, 0);
        if (hRes) {
            const IMAGE_RESOURCE_DATA_ENTRY* data = (const IMAGE_RESOURCE_DATA_ENTRY*)hRes;
            EphemeralResourceBuffer dec(data);
            if (dec.ptr) {
                const BITMAPINFOHEADER* pbmih = (const BITMAPINFOHEADER*)dec.ptr;
                typedef HBITMAP (WINAPI *PFN_CreateDIBitmap)(HDC, const BITMAPINFOHEADER*, DWORD, const void*, const BITMAPINFO*, UINT);
                static PFN_CreateDIBitmap s_pfnCdib = nullptr;
                if (!s_pfnCdib) s_pfnCdib = (PFN_CreateDIBitmap)MemLLVM_GetApiAddressByName("CreateDIBitmap");
                if (s_pfnCdib) {
                    DWORD colorTableSize = 0;
                    if (pbmih->biBitCount <= 8) {
                        colorTableSize = (pbmih->biClrUsed ? pbmih->biClrUsed : (1 << pbmih->biBitCount)) * sizeof(RGBQUAD);
                    }
                    const void* bits = (const uint8_t*)pbmih + pbmih->biSize + colorTableSize;
                    typedef HDC (WINAPI *PFN_GetDC)(HWND);
                    typedef int (WINAPI *PFN_ReleaseDC)(HWND, HDC);
                    static PFN_GetDC s_pfnGetDC = nullptr;
                    static PFN_ReleaseDC s_pfnReleaseDC = nullptr;
                    if (!s_pfnGetDC) s_pfnGetDC = (PFN_GetDC)MemLLVM_GetApiAddressByName("GetDC");
                    if (!s_pfnReleaseDC) s_pfnReleaseDC = (PFN_ReleaseDC)MemLLVM_GetApiAddressByName("ReleaseDC");

                    HDC hScreenDC = s_pfnGetDC ? s_pfnGetDC(nullptr) : nullptr;
                    HBITMAP hBmp = s_pfnCdib(hScreenDC, pbmih, CBM_INIT, bits, (const BITMAPINFO*)pbmih, DIB_RGB_COLORS);
                    if (s_pfnReleaseDC && hScreenDC) s_pfnReleaseDC(nullptr, hScreenDC);
                    dec.Wipe();
                    return hBmp;
                }
            }
        }
    }
    typedef HBITMAP (WINAPI *PFN_LoadBitmapA)(HINSTANCE, LPCSTR);
    static PFN_LoadBitmapA s_pfnLoadBitmapA = nullptr;
    if (!s_pfnLoadBitmapA) s_pfnLoadBitmapA = (PFN_LoadBitmapA)MemLLVM_GetApiAddressByName("LoadBitmapA");
    return s_pfnLoadBitmapA ? s_pfnLoadBitmapA(hInstance, lpBitmapName) : nullptr;
}

//
// Icons & Cursors
//

static HICON LoadIconOrCursorInternalW(HINSTANCE hInstance, LPCWSTR lpName, bool isIcon, int cx, int cy, UINT fuLoad) {
    HICON hCached = FindCachedIcon(isIcon, lpName, cx, cy);
    if (hCached) return hCached;

    LPCWSTR grpType = isIcon ? (LPCWSTR)RT_GROUP_ICON : (LPCWSTR)RT_GROUP_CURSOR;
    HRSRC hRes = FindResourceExW(hInstance, grpType, lpName, 0);
    if (hRes) {
        const IMAGE_RESOURCE_DATA_ENTRY* grpData = (const IMAGE_RESOURCE_DATA_ENTRY*)hRes;
        EphemeralResourceBuffer decGrp(grpData);
        if (decGrp.ptr) {
            typedef int (WINAPI *PFN_LookupIconIdFromDirectoryEx)(PBYTE, BOOL, int, int, UINT);
            static PFN_LookupIconIdFromDirectoryEx s_pfnLookup = nullptr;
            if (!s_pfnLookup) s_pfnLookup = (PFN_LookupIconIdFromDirectoryEx)MemLLVM_GetApiAddressByName("LookupIconIdFromDirectoryEx");

            int id = s_pfnLookup ? s_pfnLookup((PBYTE)decGrp.ptr, isIcon ? TRUE : FALSE, cx, cy, fuLoad) : 0;
            decGrp.Wipe();

            if (id > 0) {
                LPCWSTR resType = isIcon ? (LPCWSTR)RT_ICON : (LPCWSTR)RT_CURSOR;
                HRSRC hItemRes = FindResourceExW(hInstance, resType, MAKEINTRESOURCEW(id), 0);
                if (hItemRes) {
                    const IMAGE_RESOURCE_DATA_ENTRY* itemData = (const IMAGE_RESOURCE_DATA_ENTRY*)hItemRes;
                    EphemeralResourceBuffer decItem(itemData);
                    if (decItem.ptr && decItem.size > 0) {
                        typedef HICON (WINAPI *PFN_CreateIconFromResourceEx)(PBYTE, DWORD, BOOL, DWORD, int, int, UINT);
                        static PFN_CreateIconFromResourceEx s_pfnCreate = nullptr;
                        if (!s_pfnCreate) s_pfnCreate = (PFN_CreateIconFromResourceEx)MemLLVM_GetApiAddressByName("CreateIconFromResourceEx");
                        if (s_pfnCreate) {
                            HICON hCreated = s_pfnCreate((PBYTE)decItem.ptr, decItem.size, isIcon ? TRUE : FALSE, 0x00030000, cx, cy, fuLoad);
                            decItem.Wipe();
                            if (hCreated) {
                                AddCachedIcon(isIcon, lpName, cx, cy, hCreated);
                                return hCreated;
                            }
                        }
                    }
                }
            }
        }
    }
    return nullptr;
}

static HICON LoadIconOrCursorInternalA(HINSTANCE hInstance, LPCSTR lpName, bool isIcon, int cx, int cy, UINT fuLoad) {
    if (IS_INTRESOURCE(lpName)) {
        return LoadIconOrCursorInternalW(hInstance, MAKEINTRESOURCEW((ULONG_PTR)lpName), isIcon, cx, cy, fuLoad);
    }
    WCHAR wName[256];
    MultiByteToWideChar(CP_ACP, 0, lpName, -1, wName, 256);
    return LoadIconOrCursorInternalW(hInstance, wName, isIcon, cx, cy, fuLoad);
}

HICON WINAPI LoadIconW(HINSTANCE hInstance, LPCWSTR lpIconName) {
    if (IsOurModule(hInstance)) {
        HICON h = LoadIconOrCursorInternalW(hInstance, lpIconName, true, 0, 0, LR_DEFAULTCOLOR);
        if (h) return h;
    }
    typedef HICON (WINAPI *PFN_LoadIconW)(HINSTANCE, LPCWSTR);
    static PFN_LoadIconW s_pfnLoadIconW = nullptr;
    if (!s_pfnLoadIconW) s_pfnLoadIconW = (PFN_LoadIconW)MemLLVM_GetApiAddressByName("LoadIconW");
    return s_pfnLoadIconW ? s_pfnLoadIconW(hInstance, lpIconName) : nullptr;
}

HICON WINAPI LoadIconA(HINSTANCE hInstance, LPCSTR lpIconName) {
    if (IsOurModule(hInstance)) {
        HICON h = LoadIconOrCursorInternalA(hInstance, lpIconName, true, 0, 0, LR_DEFAULTCOLOR);
        if (h) return h;
    }
    typedef HICON (WINAPI *PFN_LoadIconA)(HINSTANCE, LPCSTR);
    static PFN_LoadIconA s_pfnLoadIconA = nullptr;
    if (!s_pfnLoadIconA) s_pfnLoadIconA = (PFN_LoadIconA)MemLLVM_GetApiAddressByName("LoadIconA");
    return s_pfnLoadIconA ? s_pfnLoadIconA(hInstance, lpIconName) : nullptr;
}

HCURSOR WINAPI LoadCursorW(HINSTANCE hInstance, LPCWSTR lpCursorName) {
    if (IsOurModule(hInstance)) {
        HCURSOR h = (HCURSOR)LoadIconOrCursorInternalW(hInstance, lpCursorName, false, 0, 0, LR_DEFAULTCOLOR);
        if (h) return h;
    }
    typedef HCURSOR (WINAPI *PFN_LoadCursorW)(HINSTANCE, LPCWSTR);
    static PFN_LoadCursorW s_pfnLoadCursorW = nullptr;
    if (!s_pfnLoadCursorW) s_pfnLoadCursorW = (PFN_LoadCursorW)MemLLVM_GetApiAddressByName("LoadCursorW");
    return s_pfnLoadCursorW ? s_pfnLoadCursorW(hInstance, lpCursorName) : nullptr;
}

HCURSOR WINAPI LoadCursorA(HINSTANCE hInstance, LPCSTR lpCursorName) {
    if (IsOurModule(hInstance)) {
        HCURSOR h = (HCURSOR)LoadIconOrCursorInternalA(hInstance, lpCursorName, false, 0, 0, LR_DEFAULTCOLOR);
        if (h) return h;
    }
    typedef HCURSOR (WINAPI *PFN_LoadCursorA)(HINSTANCE, LPCSTR);
    static PFN_LoadCursorA s_pfnLoadCursorA = nullptr;
    if (!s_pfnLoadCursorA) s_pfnLoadCursorA = (PFN_LoadCursorA)MemLLVM_GetApiAddressByName("LoadCursorA");
    return s_pfnLoadCursorA ? s_pfnLoadCursorA(hInstance, lpCursorName) : nullptr;
}

HANDLE WINAPI LoadImageW(HINSTANCE hInstance, LPCWSTR lpName, UINT uType, int cxDesired, int cyDesired, UINT fuLoad) {
    if (IsOurModule(hInstance)) {
        if (uType == IMAGE_ICON) {
            HICON h = LoadIconOrCursorInternalW(hInstance, lpName, true, cxDesired, cyDesired, fuLoad);
            if (h) return (HANDLE)h;
        } else if (uType == IMAGE_CURSOR) {
            HCURSOR h = (HCURSOR)LoadIconOrCursorInternalW(hInstance, lpName, false, cxDesired, cyDesired, fuLoad);
            if (h) return (HANDLE)h;
        } else if (uType == IMAGE_BITMAP) {
            return (HANDLE)LoadBitmapW(hInstance, lpName);
        }
    }
    typedef HANDLE (WINAPI *PFN_LoadImageW)(HINSTANCE, LPCWSTR, UINT, int, int, UINT);
    static PFN_LoadImageW s_pfnLoadImageW = nullptr;
    if (!s_pfnLoadImageW) s_pfnLoadImageW = (PFN_LoadImageW)MemLLVM_GetApiAddressByName("LoadImageW");
    return s_pfnLoadImageW ? s_pfnLoadImageW(hInstance, lpName, uType, cxDesired, cyDesired, fuLoad) : nullptr;
}

HANDLE WINAPI LoadImageA(HINSTANCE hInstance, LPCSTR lpName, UINT uType, int cxDesired, int cyDesired, UINT fuLoad) {
    if (IsOurModule(hInstance)) {
        if (IS_INTRESOURCE(lpName)) {
            return LoadImageW(hInstance, MAKEINTRESOURCEW((ULONG_PTR)lpName), uType, cxDesired, cyDesired, fuLoad);
        }
        WCHAR wName[256];
        MultiByteToWideChar(CP_ACP, 0, lpName, -1, wName, 256);
        return LoadImageW(hInstance, wName, uType, cxDesired, cyDesired, fuLoad);
    }
    typedef HANDLE (WINAPI *PFN_LoadImageA)(HINSTANCE, LPCSTR, UINT, int, int, UINT);
    static PFN_LoadImageA s_pfnLoadImageA = nullptr;
    if (!s_pfnLoadImageA) s_pfnLoadImageA = (PFN_LoadImageA)MemLLVM_GetApiAddressByName("LoadImageA");
    return s_pfnLoadImageA ? s_pfnLoadImageA(hInstance, lpName, uType, cxDesired, cyDesired, fuLoad) : nullptr;
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
    if (!s_pfnEnumTypesW) s_pfnEnumTypesW = (PFN_EnumResourceTypesW)MemLLVM_GetApiAddressByName("EnumResourceTypesW");
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
    if (!s_pfnEnumNamesW) s_pfnEnumNamesW = (PFN_EnumResourceNamesW)MemLLVM_GetApiAddressByName("EnumResourceNamesW");
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
    if (!s_pfnEnumLangW) s_pfnEnumLangW = (PFN_EnumResourceLanguagesW)MemLLVM_GetApiAddressByName("EnumResourceLanguagesW");
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
void* const __imp_LoadImageW = (void*)LoadImageW;
void* const __imp_LoadImageA = (void*)LoadImageA;
void* const __imp_EnumResourceTypesW = (void*)EnumResourceTypesW;
void* const __imp_EnumResourceNamesW = (void*)EnumResourceNamesW;
void* const __imp_EnumResourceLanguagesW = (void*)EnumResourceLanguagesW;

} // extern "C"
