/*
 * Resolver.c —— MemLLVM_GetApiAddress 的实现
 *
 * 编译成 bitcode 后由 ApiResolvePass 链接进每个 TU（见 ResolverBitcode.h）。
 *
 * 契约：
 *     void *MemLLVM_GetApiAddress(unsigned int ror13Hash);
 *
 * 行为：
 *   1. 从 gs:[0x60] 取 PEB，走 Ldr->InLoadOrderModuleList 遍历已加载模块；
 *   2. 对每个模块解析导出表，逐个导出名算 ROR13，命中即返回 ImageBase+RVA；
 *   3. 命中项若是 forwarder（函数 RVA 落在导出目录区间内，内容形如
 *      "NTDLL.RtlInitializeCriticalSection"），解析出目标 DLL 与函数名，
 *      加载目标 DLL 后递归查找；
 *   4. 全部模块都找不到则返回 NULL。
 *
 * 为什么必须有第 3 步：Win10/11 的 kernel32.dll 把大量 API 做成了 forwarder
 * （CreateFileW、InitializeCriticalSection 等都是），实现在 kernelbase/ntdll。
 * 早先的版本遇到 forwarder 直接跳过，于是这些 API 一律解析成 NULL，调用点
 * `call 0` 立刻崩溃。
 *
 * 为什么必须有深度上限：forwarder 理论上可以成环（A!Foo -> B!Foo -> A!Foo），
 * 或者转发目标名被误解析后重新命中同一个 forwarder。没有上限时递归会一路吃
 * 掉线程栈，最终以 0xC00000FD（STATUS_STACK_OVERFLOW）终止进程。
 *
 * 无 CRT 依赖：只用内联汇编读 PEB 和手写循环，不引用 memcpy/memset。
 */

typedef unsigned int uint32_t;
typedef unsigned short uint16_t;
typedef unsigned char uint8_t;
typedef unsigned long long uint64_t;

typedef struct _LIST_ENTRY {
  struct _LIST_ENTRY *Flink;
  struct _LIST_ENTRY *Blink;
} LIST_ENTRY;

typedef struct _UNICODE_STRING {
  uint16_t Length;
  uint16_t MaximumLength;
  uint16_t *Buffer;
} UNICODE_STRING;

typedef struct _LDR_DATA_TABLE_ENTRY {
  LIST_ENTRY InLoadOrderLinks;             /* +0x00 */
  LIST_ENTRY InMemoryOrderLinks;           /* +0x10 */
  LIST_ENTRY InInitializationOrderLinks;   /* +0x20 */
  void *DllBase;                           /* +0x30 */
  void *EntryPoint;                        /* +0x38 */
  uint32_t SizeOfImage;                    /* +0x40 */
  uint32_t _padding;                       /* +0x44 */
  UNICODE_STRING FullDllName;              /* +0x48 */
  UNICODE_STRING BaseDllName;              /* +0x58 */
} LDR_DATA_TABLE_ENTRY;

typedef struct _PEB_LDR_DATA {
  uint32_t Length;                         /* +0x00 */
  uint32_t Initialized;                    /* +0x04 */
  void *SsHandle;                          /* +0x08 */
  LIST_ENTRY InLoadOrderModuleList;        /* +0x10 */
  LIST_ENTRY InMemoryOrderModuleList;      /* +0x20 */
  LIST_ENTRY InInitializationOrderList;    /* +0x30 */
} PEB_LDR_DATA;

typedef struct _IMAGE_EXPORT_DIRECTORY {
  uint32_t Characteristics;                /* +0x00 */
  uint32_t TimeDateStamp;                  /* +0x04 */
  uint16_t MajorVersion;                   /* +0x08 */
  uint16_t MinorVersion;                   /* +0x0A */
  uint32_t Name;                           /* +0x0C */
  uint32_t Base;                           /* +0x10 */
  uint32_t NumberOfFunctions;              /* +0x14 */
  uint32_t NumberOfNames;                  /* +0x18 */
  uint32_t AddressOfFunctions;             /* +0x1C */
  uint32_t AddressOfNames;                 /* +0x20 */
  uint32_t AddressOfNameOrdinals;          /* +0x24 */
} IMAGE_EXPORT_DIRECTORY;

/* LoadLibraryA 的 ROR13，用于 forwarder 解析时按需加载目标 DLL */
#define HASH_LOADLIBRARYA 0xEC0E4E8E

/* forwarder 链的最大深度，防御成环导致的无限递归 */
#define MAX_FORWARD_DEPTH 8

static void *memllvm_resolve(uint32_t hash, int depth);

static uint8_t *memllvm_get_peb(void) {
  uint8_t *peb;
  __asm__ __volatile__("movq %%gs:0x60, %0" : "=r"(peb));
  return peb;
}

/* 循环右移 13 位后累加字节，等价于 RtlComputeHash(ROR13) */
static uint32_t memllvm_ror13(const char *name) {
  uint32_t hash = 0;
  while (*name) {
    hash = (hash >> 13) | (hash << 19);
    hash += (uint8_t)*name;
    ++name;
  }
  return hash;
}

/*
 * 在一个模块的导出表里按哈希查找。
 * 返回值：命中则返回运行时函数地址；未命中返回 0。
 * 若命中项是 forwarder，会解析转发目标并递归（受 MAX_FORWARD_DEPTH 限制）。
 */
static void *memllvm_lookup_in_module(uint8_t *base, uint32_t hash, int depth) {
  uint32_t pe_offset;
  uint32_t export_rva, export_size;
  IMAGE_EXPORT_DIRECTORY *dir;
  uint32_t *names, *functions;
  uint16_t *ordinals;
  uint32_t i;

  if (!base)
    return 0;
  if (*(uint16_t *)base != 0x5A4D) /* "MZ" */
    return 0;

  pe_offset = *(uint32_t *)(base + 0x3C);
  if (*(uint32_t *)(base + pe_offset) != 0x00004550) /* "PE\0\0" */
    return 0;

  /* x64 OptionalHeader.DataDirectory[0] = Export，位于 PE + 0x18 + 0x70 */
  export_rva = *(uint32_t *)(base + pe_offset + 0x88);
  export_size = *(uint32_t *)(base + pe_offset + 0x8C);
  if (!export_rva || !export_size)
    return 0;

  dir = (IMAGE_EXPORT_DIRECTORY *)(base + export_rva);
  names = (uint32_t *)(base + dir->AddressOfNames);
  functions = (uint32_t *)(base + dir->AddressOfFunctions);
  ordinals = (uint16_t *)(base + dir->AddressOfNameOrdinals);

  for (i = 0; i < dir->NumberOfNames; ++i) {
    const char *export_name = (const char *)(base + names[i]);
    uint32_t function_rva;

    if (memllvm_ror13(export_name) != hash)
      continue;

    function_rva = functions[ordinals[i]];

    /* forwarder：RVA 指回导出目录内部，内容是 "DLL.Function" 字符串 */
    if (function_rva >= export_rva && function_rva < export_rva + export_size) {
      const char *forward = (const char *)(base + function_rva);
      char dll_name[64];
      int n = 0;
      void *(*load_library_a)(const char *);
      uint8_t *target_base;

      if (depth >= MAX_FORWARD_DEPTH)
        return 0;

      while (forward[n] && forward[n] != '.' && n < 58) {
        dll_name[n] = forward[n];
        ++n;
      }
      dll_name[n] = '\0';

      if (forward[n] != '.')
        return 0;

      /* 用 PEB 解析出的 LoadLibraryA 加载目标 DLL，再递归查找 */
      load_library_a =
          (void *(*)(const char *))memllvm_resolve(HASH_LOADLIBRARYA, depth + 1);
      if (!load_library_a)
        return 0;

      target_base = (uint8_t *)load_library_a(dll_name);
      if (!target_base) {
        /* 尝试自动补充 .dll 后缀 */
        int len = 0;
        while (dll_name[len]) len++;
        dll_name[len] = '.';
        dll_name[len + 1] = 'd';
        dll_name[len + 2] = 'l';
        dll_name[len + 3] = 'l';
        dll_name[len + 4] = '\0';
        target_base = (uint8_t *)load_library_a(dll_name);
      }

      /* 处理按序号转发，例如 "SHLWAPI.#12" */
      if (forward[n + 1] == '#') {
        uint32_t ord = 0;
        const char *p = forward + n + 2;
        while (*p >= '0' && *p <= '9') {
          ord = ord * 10 + (*p - '0');
          ++p;
        }
        if (target_base) {
          uint32_t t_pe = *(uint32_t *)(target_base + 0x3C);
          uint32_t t_exp_rva = *(uint32_t *)(target_base + t_pe + 0x88);
          if (t_exp_rva) {
            IMAGE_EXPORT_DIRECTORY *t_dir = (IMAGE_EXPORT_DIRECTORY *)(target_base + t_exp_rva);
            if (ord >= t_dir->Base && ord < t_dir->Base + t_dir->NumberOfFunctions) {
              uint32_t *t_funcs = (uint32_t *)(target_base + t_dir->AddressOfFunctions);
              return target_base + t_funcs[ord - t_dir->Base];
            }
          }
        }
        return 0;
      }

      return memllvm_resolve(memllvm_ror13(forward + n + 1), depth + 1);
    }

    return base + function_rva;
  }

  return 0;
}

static void *memllvm_resolve(uint32_t hash, int depth) {
  uint8_t *peb;
  uint8_t *ldr;
  LIST_ENTRY *head, *cursor;

  if (depth > MAX_FORWARD_DEPTH)
    return 0;

  peb = memllvm_get_peb();
  if (!peb)
    return 0;

  ldr = *(uint8_t **)(peb + 0x18);
  if (!ldr)
    return 0;

  head = (LIST_ENTRY *)(ldr + 0x10); /* InLoadOrderModuleList */
  cursor = head->Flink;

  while (cursor && cursor != head) {
    LDR_DATA_TABLE_ENTRY *entry = (LDR_DATA_TABLE_ENTRY *)cursor;
    void *found =
        memllvm_lookup_in_module((uint8_t *)entry->DllBase, hash, depth);
    if (found)
      return found;
    cursor = cursor->Flink;
  }

  return 0;
}

void *MemLLVM_GetApiAddress(uint32_t ror13_hash) {
  return memllvm_resolve(ror13_hash, 0);
}
