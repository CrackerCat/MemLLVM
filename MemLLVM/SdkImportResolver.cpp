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
#include <cstdlib>
#include <cstring>
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
  // Scanning every import library is expensive, and the result does not depend
  // on the name being looked up, so it is done at most once per process.
  bool LocalLibsScanned = false;
  std::vector<std::string> DllNames;
  std::vector<SdkApiEntry> Entries;
  std::unordered_map<std::string, std::string> DynamicCache;
  std::unordered_map<std::string, bool> ApiCheckCache;
  std::unordered_map<std::string, std::string> ApiDllCache;

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

  // Parse a single COFF import library and cache every (symbol -> dll) pair it
  // declares. Shared by every directory searched below.
  //
  // The scan walks the raw bytes looking for IMPORT_OBJECT_HEADER signatures
  // rather than parsing the archive properly, so every read has to be bounded
  // by the buffer end. The previous version required 32 spare bytes past the
  // header, which silently dropped the *last* import object of a short library:
  // msasn1.min.lib ends with ASN1_FreeEncoded at offset 3536 of 3584, leaving
  // only 48 bytes and putting it outside the loop bound. That one symbol was
  // then never resolvable, and mimikatz kept importing it.
  void scanOneLib(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
      return;
    fseek(f, 0, SEEK_END);
    long rawSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (rawSize <= 0 || rawSize >= 100 * 1024 * 1024) {
      fclose(f);
      return;
    }

    std::vector<uint8_t> buf((size_t)rawSize);
    bool ok = fread(buf.data(), 1, (size_t)rawSize, f) == (size_t)rawSize;
    fclose(f);
    if (!ok)
      return;

    const char *base = (const char*)buf.data();
    const char *end = base + buf.size();

    for (size_t i = 0; i + sizeof(IMPORT_OBJECT_HEADER) < buf.size(); i++) {
      auto *hdr = (const IMPORT_OBJECT_HEADER*)(base + i);
      if (hdr->Sig1 != 0 || hdr->Sig2 != 0xFFFF)
        continue;
      if (hdr->Machine != 0x8664 && hdr->Machine != 0)
        continue;

      const char *sym = base + i + sizeof(IMPORT_OBJECT_HEADER);
      if (sym >= end)
        continue;
      size_t symLen = strnlen(sym, std::min<size_t>((size_t)(end - sym), 128));
      if (symLen == 0 || symLen >= (size_t)(end - sym))
        continue;

      const char *dll = sym + symLen + 1;
      if (dll >= end)
        continue;
      size_t dllLen = strnlen(dll, std::min<size_t>((size_t)(end - dll), 64));
      if (dllLen == 0 || dllLen >= (size_t)(end - dll))
        continue;

      std::string lowerDll(dll, dllLen);
      for (char &c : lowerDll) c = (char)tolower((unsigned char)c);
      if (lowerDll.find(".dll") == std::string::npos &&
          lowerDll.find(".drv") == std::string::npos)
        continue;

      DynamicCache[std::string(sym, symLen)] = lowerDll;
    }
  }

  // Undocumented exports (ASN1_*, I_NetServer*, RtlGetCurrentPeb ...) are not in
  // the Windows SDK index, so the only place their owning DLL is recorded is the
  // project's own import libraries. mimikatz keeps those in lib\x64, not in the
  // directory the compiler runs from, so the search has to cover a few likely
  // locations — plus MEMLLVM_LIBPATH for anything unusual.
  void scanLocalLibsForApi(StringRef ApiName) {
#ifdef _WIN32
    if (LocalLibsScanned)
      return;
    LocalLibsScanned = true;

    static const char *Dirs[] = {
        "",            // cwd (original behaviour)
        "lib\\",
        "lib\\x64\\",
        "..\\lib\\x64\\",
        "..\\..\\lib\\x64\\",
    };

    for (const char *Dir : Dirs) {
      std::string Pattern = std::string(Dir) + "*.lib";
      WIN32_FIND_DATAA fd;
      HANDLE hFind = FindFirstFileA(Pattern.c_str(), &fd);
      if (hFind == INVALID_HANDLE_VALUE)
        continue;
      do {
        std::string Full = std::string(Dir) + fd.cFileName;
        scanOneLib(Full.c_str());
      } while (FindNextFileA(hFind, &fd));
      FindClose(hFind);
    }

    // Explicit override: semicolon-separated list of additional directories.
    if (const char *Env = getenv("MEMLLVM_LIBPATH")) {
      std::string All(Env);
      size_t Start = 0;
      while (Start <= All.size()) {
        size_t Sep = All.find(';', Start);
        if (Sep == std::string::npos) Sep = All.size();
        std::string Dir = All.substr(Start, Sep - Start);
        if (!Dir.empty()) {
          if (Dir.back() != '\\' && Dir.back() != '/') Dir += '\\';
          std::string Pattern = Dir + "*.lib";
          WIN32_FIND_DATAA fd;
          HANDLE hFind = FindFirstFileA(Pattern.c_str(), &fd);
          if (hFind != INVALID_HANDLE_VALUE) {
            do {
              std::string Full = Dir + fd.cFileName;
              scanOneLib(Full.c_str());
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
          }
        }
        if (Sep == All.size()) break;
        Start = Sep + 1;
      }
    }
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
    if (ApiName.empty() || ApiName.starts_with("?") || ApiName.starts_with("llvm."))
      return "";

    std::string s = ApiName.str();
    auto cacheIt = ApiDllCache.find(s);
    if (cacheIt != ApiDllCache.end())
      return cacheIt->second;

    init();

#ifdef _WIN32
    static HMODULE hK32 = GetModuleHandleA("kernel32.dll");
    static HMODULE hNt = GetModuleHandleA("ntdll.dll");
    if (hK32 && GetProcAddress(hK32, s.c_str())) {
      ApiDllCache[s] = "";
      return "";
    }
    if (hNt && GetProcAddress(hNt, s.c_str())) {
      ApiDllCache[s] = "";
      return "";
    }
    static HMODULE hUcrt = LoadLibraryA("ucrtbase.dll");
    if (hUcrt && GetProcAddress(hUcrt, s.c_str())) {
      ApiDllCache[s] = "ucrtbase.dll";
      return "ucrtbase.dll";
    }
#endif

    auto normalizeDll = [](const std::string &dll) -> std::string {
      if (dll == "kernel32.dll" || dll == "ntdll.dll")
        return "";
      if (dll == "ucrtbased.dll" || dll == "ucrtbase.dll" || dll.rfind("api-ms-win-crt-", 0) == 0)
        return "ucrtbase.dll";
      return dll;
    };

    // Fast binary search in Windows SDK index
    uint32_t h = calculateRor13(ApiName);
    auto it = std::lower_bound(Entries.begin(), Entries.end(), h, [](const SdkApiEntry &e, uint32_t val) {
      return e.hash < val;
    });
    if (it != Entries.end() && it->hash == h) {
      std::string res = normalizeDll(DllNames[it->dllIndex]);
      ApiDllCache[s] = res;
      return res;
    }

    // Check dynamic cache (e.g. from local scanned .libs)
    auto dynIt = DynamicCache.find(s);
    if (dynIt != DynamicCache.end()) {
      std::string res = normalizeDll(dynIt->second);
      ApiDllCache[s] = res;
      return res;
    }

    // Fallback: on-demand scan *.lib in current directory
    scanLocalLibsForApi(ApiName);
    dynIt = DynamicCache.find(s);
    if (dynIt != DynamicCache.end()) {
      std::string res = normalizeDll(dynIt->second);
      ApiDllCache[s] = res;
      return res;
    }

    ApiDllCache[s] = "";
    return "";
  }

  bool isApi(StringRef ApiName) {
    if (ApiName.empty() || ApiName.starts_with("?") || ApiName.starts_with("llvm."))
      return false;

    std::string s = ApiName.str();
    auto cacheIt = ApiCheckCache.find(s);
    if (cacheIt != ApiCheckCache.end())
      return cacheIt->second;

#ifdef _WIN32
    static HMODULE hK32 = GetModuleHandleA("kernel32.dll");
    static HMODULE hNt = GetModuleHandleA("ntdll.dll");
    if (hK32 && GetProcAddress(hK32, s.c_str())) {
      ApiCheckCache[s] = true;
      return true;
    }
    if (hNt && GetProcAddress(hNt, s.c_str())) {
      ApiCheckCache[s] = true;
      return true;
    }
    static HMODULE hUcrt = LoadLibraryA("ucrtbase.dll");
    if (hUcrt && GetProcAddress(hUcrt, s.c_str())) {
      ApiCheckCache[s] = true;
      return true;
    }
#endif

    init();
    uint32_t h = calculateRor13(ApiName);
    auto it = std::lower_bound(Entries.begin(), Entries.end(), h, [](const SdkApiEntry &e, uint32_t val) {
      return e.hash < val;
    });
    if (it != Entries.end() && it->hash == h) {
      ApiCheckCache[s] = true;
      return true;
    }

    auto dynIt = DynamicCache.find(s);
    if (dynIt != DynamicCache.end()) {
      ApiCheckCache[s] = true;
      return true;
    }

    scanLocalLibsForApi(ApiName);
    dynIt = DynamicCache.find(s);
    if (dynIt != DynamicCache.end()) {
      ApiCheckCache[s] = true;
      return true;
    }

    ApiCheckCache[s] = false;
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
