#include "SdkImportResolver.h"
#include "MemLLVMUtils.h"
#include "SdkImportIndexData.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

using namespace llvm;

namespace memllvm {

#pragma pack(push, 1)
struct SdkApiEntry {
  uint32_t hash;
  uint16_t dllIndex;
};
#pragma pack(pop)

class SdkImportResolver {
private:
  bool Initialized = false;
  std::vector<std::string> DllNames;
  std::vector<SdkApiEntry> Entries;
  std::unordered_map<std::string, std::string> DynamicCache;

  bool loadFromMemory(const uint8_t *data, size_t size) {
    if (size < 16) return false;
    uint32_t magic = *(const uint32_t*)(data);
    uint32_t version = *(const uint32_t*)(data + 4);
    uint32_t numDlls = *(const uint32_t*)(data + 8);
    uint32_t numEntries = *(const uint32_t*)(data + 12);
    if (magic != 0x4D454D49 || version != 2) return false;

    size_t offset = 16;
    DllNames.resize(numDlls);
    for (uint32_t i = 0; i < numDlls; i++) {
      if (offset + 2 > size) return false;
      uint16_t len = *(const uint16_t*)(data + offset);
      offset += 2;
      if (offset + len > size) return false;
      DllNames[i] = std::string((const char*)(data + offset), len);
      offset += len;
    }

    if (offset + numEntries * sizeof(SdkApiEntry) > size) return false;
    const SdkApiEntry *entryPtr = (const SdkApiEntry*)(data + offset);
    Entries.assign(entryPtr, entryPtr + numEntries);
    return true;
  }

  void scanLocalLibsForApi(StringRef ApiName) {
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA("*.lib", &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
      FILE *f = fopen(fd.cFileName, "rb");
      if (!f) continue;
      fseek(f, 0, SEEK_END);
      size_t sz = ftell(f);
      fseek(f, 0, SEEK_SET);
      if (sz > 0 && sz < 100 * 1024 * 1024) {
        std::vector<uint8_t> buf(sz);
        if (fread(buf.data(), 1, sz, f) == sz) {
          for (size_t i = 0; i + sizeof(IMPORT_OBJECT_HEADER) + 32 < sz; i++) {
            auto *hdr = (const IMPORT_OBJECT_HEADER*)(buf.data() + i);
            if (hdr->Sig1 == 0 && hdr->Sig2 == 0xFFFF && (hdr->Machine == 0x8664 || hdr->Machine == 0)) {
              const char *sym = (const char*)(buf.data() + i + sizeof(IMPORT_OBJECT_HEADER));
              size_t symLen = strlen(sym);
              if (symLen > 0 && symLen < 128) {
                const char *dll = sym + symLen + 1;
                if ((const uint8_t*)dll + strlen(dll) < buf.data() + sz && strlen(dll) > 0 && strlen(dll) < 64) {
                  if (strstr(dll, ".dll") || strstr(dll, ".DLL") || strstr(dll, ".drv") || strstr(dll, ".DRV")) {
                    std::string lowerDll = dll;
                    for (char &c : lowerDll) c = tolower(c);
                    DynamicCache[sym] = lowerDll;
                  }
                }
              }
            }
          }
        }
      }
      fclose(f);
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
#endif
  }

public:
  static SdkImportResolver &get() {
    static SdkImportResolver instance;
    return instance;
  }

  void init() {
    if (Initialized) return;
    Initialized = true;

    // 1. Try external file next to current directory or D:\MemLLVM
    bool loaded = false;
    const char *paths[] = {
      "SdkImportIndex.dat",
      "../SdkImportIndex.dat",
      "D:\\MemLLVM\\SdkImportIndex.dat"
    };
    for (const char *p : paths) {
      FILE *f = fopen(p, "rb");
      if (f) {
        fseek(f, 0, SEEK_END);
        size_t sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz > 0 && sz < 10 * 1024 * 1024) {
          std::vector<uint8_t> buf(sz);
          if (fread(buf.data(), 1, sz, f) == sz) {
            if (loadFromMemory(buf.data(), sz)) {
              loaded = true;
              fclose(f);
              break;
            }
          }
        }
        fclose(f);
      }
    }

    // 2. Fall back to embedded database if file not loaded
    if (!loaded) {
      loadFromMemory(g_SdkImportIndexData, g_SdkImportIndexSize);
    }
  }

  std::string resolve(StringRef ApiName) {
    init();

#ifdef _WIN32
    static HMODULE hK32 = GetModuleHandleA("kernel32.dll");
    static HMODULE hNt = GetModuleHandleA("ntdll.dll");
    std::string s = ApiName.str();
    if (hK32 && GetProcAddress(hK32, s.c_str())) return "";
    if (hNt && GetProcAddress(hNt, s.c_str())) return "";
#endif

    auto normalizeDll = [](const std::string &dll) -> std::string {
      if (dll == "kernel32.dll" || dll == "ntdll.dll")
        return "";
      if (dll == "ucrtbased.dll" || dll == "ucrtbase.dll" || dll.rfind("api-ms-win-crt-", 0) == 0)
        return "msvcrt.dll";
      return dll;
    };

    // Fast binary search in Windows SDK index
    uint32_t h = calculateRor13(ApiName);
    auto it = std::lower_bound(Entries.begin(), Entries.end(), h, [](const SdkApiEntry &e, uint32_t val) {
      return e.hash < val;
    });
    if (it != Entries.end() && it->hash == h) {
      return normalizeDll(DllNames[it->dllIndex]);
    }

    // Check dynamic cache (e.g. from local scanned .libs)
    auto dynIt = DynamicCache.find(ApiName.str());
    if (dynIt != DynamicCache.end()) {
      return normalizeDll(dynIt->second);
    }

    // Fallback: on-demand scan *.lib in current directory
    scanLocalLibsForApi(ApiName);
    dynIt = DynamicCache.find(ApiName.str());
    if (dynIt != DynamicCache.end()) {
      return normalizeDll(dynIt->second);
    }

    return "";
  }

  bool isApi(StringRef ApiName) {
    if (ApiName.empty() || ApiName.starts_with("?") || ApiName.starts_with("llvm."))
      return false;

#ifdef _WIN32
    static HMODULE hK32 = GetModuleHandleA("kernel32.dll");
    static HMODULE hNt = GetModuleHandleA("ntdll.dll");
    std::string s = ApiName.str();
    if (hK32 && GetProcAddress(hK32, s.c_str())) return true;
    if (hNt && GetProcAddress(hNt, s.c_str())) return true;
#endif

    init();
    uint32_t h = calculateRor13(ApiName);
    auto it = std::lower_bound(Entries.begin(), Entries.end(), h, [](const SdkApiEntry &e, uint32_t val) {
      return e.hash < val;
    });
    if (it != Entries.end() && it->hash == h) {
      return true;
    }

    auto dynIt = DynamicCache.find(ApiName.str());
    if (dynIt != DynamicCache.end()) {
      return true;
    }

    scanLocalLibsForApi(ApiName);
    dynIt = DynamicCache.find(ApiName.str());
    if (dynIt != DynamicCache.end()) {
      return true;
    }

    return false;
  }
};

std::string probeRequiredDllForApi(StringRef ApiName) {
  return SdkImportResolver::get().resolve(ApiName);
}

bool isKnownWindowsApi(StringRef ApiName) {
  return SdkImportResolver::get().isApi(ApiName);
}

} // namespace memllvm
