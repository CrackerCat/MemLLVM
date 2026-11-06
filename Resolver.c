/*
 * Resolver.c — Hyper-compact unified MemLLVM_GetApiAddress & MemLLVM_GetApiAddressByOrdinal
 * with direct forwarder and ordinal resolution
 */

typedef unsigned int uint32_t;
typedef unsigned short uint16_t;
typedef unsigned char uint8_t;
typedef unsigned long long uintptr_t;

#define MAX_FORWARD_DEPTH 4

/*
 * Dynamic hash configuration parameters injected per-build by ApiResolvePass.
 * Defaults are classic ROR13 parameters for zero-friction fallback.
 */
volatile uint32_t g_memllvm_hash_seed = 0;
volatile uint32_t g_memllvm_hash_rot  = 13;
volatile uint32_t g_memllvm_hash_mul  = 1;
volatile uint32_t g_memllvm_hash_xor  = 0;
volatile uint32_t g_memllvm_hash_loadlibrarya = 0xEC0E4E8E;

static inline uint32_t memllvm_hash(const char *s) {
  uint32_t h = g_memllvm_hash_seed;
  uint32_t rot = g_memllvm_hash_rot;
  uint32_t mul = g_memllvm_hash_mul;
  uint32_t xor_val = g_memllvm_hash_xor;
  while (*s) {
    h = (h >> rot) | (h << (32 - rot));
    h += ((uint8_t)*s++ * mul) ^ xor_val;
  }
  return h;
}

static inline uint32_t memllvm_hash_wide_lower(const uint16_t *ws, uint32_t len) {
  uint32_t h = g_memllvm_hash_seed;
  uint32_t rot = g_memllvm_hash_rot;
  uint32_t mul = g_memllvm_hash_mul;
  uint32_t xor_val = g_memllvm_hash_xor;
  for (uint32_t i = 0; i < len; ++i) {
    uint32_t c = ws[i];
    if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
    h = (h >> rot) | (h << (32 - rot));
    h += ((uint8_t)c * mul) ^ xor_val;
  }
  return h;
}

static void *memllvm_find(uint8_t *base, uint32_t hash, int depth);
static void *memllvm_find_ordinal(uint8_t *base, uint32_t ordinal, int depth);

static uint8_t *memllvm_find_module(uint32_t dll_hash) {
  uint8_t *peb;
  __asm__ volatile("movq %%gs:0x60, %0" : "=r"(peb));
  if (!peb) return 0;
  uint8_t *ldr = *(uint8_t **)(peb + 0x18);
  if (!ldr) return 0;
  uint8_t *head = ldr + 0x10;
  uint8_t *cur = *(uint8_t **)head;
  while (cur && cur != head) {
    uint8_t *b = *(uint8_t **)(cur + 0x30);
    if (dll_hash) {
      uint16_t len = *(uint16_t *)(cur + 0x58);
      uint16_t *buf = *(uint16_t **)(cur + 0x60);
      if (buf && len && memllvm_hash_wide_lower(buf, len / 2) == dll_hash) {
        return b;
      }
    }
    cur = *(uint8_t **)cur;
  }
  return 0;
}

static void *memllvm_resolve_forwarder(uint8_t *base, uint32_t fn_rva, uint32_t exp_rva, uint32_t exp_sz, int depth) {
  if ((uint32_t)(fn_rva - exp_rva) >= exp_sz)
    return base + fn_rva;

  const char *fwd = (const char *)(base + fn_rva);
  char buf[64];
  int n = 0;
  while (fwd[n] && fwd[n] != '.' && n < 60) {
    buf[n] = fwd[n];
    n++;
  }
  if (fwd[n] != '.') return 0;
  buf[n] = '\0';

  void *(*load_lib)(const char *) =
      (void *(*)(const char *))memllvm_find(0, g_memllvm_hash_loadlibrarya, depth + 1);
  if (!load_lib) return 0;

  uint8_t *target_base = (uint8_t *)load_lib(buf);
  if (!target_base) return 0;

  const char *target_func = fwd + n + 1;
  if (*target_func == '#') {
    uint32_t ord = 0;
    target_func++;
    while (*target_func >= '0' && *target_func <= '9') {
      ord = ord * 10 + (*target_func - '0');
      target_func++;
    }
    return memllvm_find_ordinal(target_base, ord, depth + 1);
  }

  return memllvm_find(target_base, memllvm_hash(target_func), depth + 1);
}

static void *memllvm_find_ordinal(uint8_t *base, uint32_t ordinal, int depth) {
  if (depth > MAX_FORWARD_DEPTH || !ordinal)
    return 0;

  if (!base) {
    uint8_t *peb;
    __asm__ volatile("movq %%gs:0x60, %0" : "=r"(peb));
    if (!peb) return 0;
    uint8_t *ldr = *(uint8_t **)(peb + 0x18);
    if (!ldr) return 0;
    uint8_t *head = ldr + 0x10;
    uint8_t *cur = *(uint8_t **)head;
    while (cur && cur != head) {
      uint8_t *b = *(uint8_t **)(cur + 0x30);
      cur = *(uint8_t **)cur;
      void *p = memllvm_find_ordinal(b, ordinal, depth);
      if (p) return p;
    }
    return 0;
  }

  if (*(uint16_t *)base != 0x5A4D)
    return 0;

  uint32_t pe = *(uint32_t *)(base + 0x3C);
  if (*(uint32_t *)(base + pe) != 0x4550)
    return 0;

  uint32_t exp_rva = *(uint32_t *)(base + pe + 0x88);
  uint32_t exp_sz = *(uint32_t *)(base + pe + 0x8C);
  if (!exp_rva || !exp_sz)
    return 0;

  uint8_t *dir = base + exp_rva;
  uint32_t base_ord = *(uint32_t *)(dir + 0x10);
  uint32_t num_funcs = *(uint32_t *)(dir + 0x14);
  uint32_t *funcs = (uint32_t *)(base + *(uint32_t *)(dir + 0x1C));

  if (ordinal < base_ord || (ordinal - base_ord) >= num_funcs)
    return 0;

  uint32_t fn_rva = funcs[ordinal - base_ord];
  if (!fn_rva)
    return 0;

  return memllvm_resolve_forwarder(base, fn_rva, exp_rva, exp_sz, depth);
}

static void *memllvm_find(uint8_t *base, uint32_t hash, int depth) {
  if (depth > MAX_FORWARD_DEPTH)
    return 0;

  if (!base) {
    uint8_t *peb;
    __asm__ volatile("movq %%gs:0x60, %0" : "=r"(peb));
    if (!peb) return 0;
    uint8_t *ldr = *(uint8_t **)(peb + 0x18);
    if (!ldr) return 0;
    uint8_t *head = ldr + 0x10;
    uint8_t *cur = *(uint8_t **)head;
    while (cur && cur != head) {
      uint8_t *b = *(uint8_t **)(cur + 0x30);
      cur = *(uint8_t **)cur;
      void *p = memllvm_find(b, hash, depth);
      if (p) return p;
    }
    return 0;
  }

  if (*(uint16_t *)base != 0x5A4D)
    return 0;

  uint32_t pe = *(uint32_t *)(base + 0x3C);
  if (*(uint32_t *)(base + pe) != 0x4550)
    return 0;

  uint32_t exp_rva = *(uint32_t *)(base + pe + 0x88);
  uint32_t exp_sz = *(uint32_t *)(base + pe + 0x8C);
  if (!exp_rva || !exp_sz)
    return 0;

  uint8_t *dir = base + exp_rva;
  uint32_t num_names = *(uint32_t *)(dir + 0x18);
  uint32_t *funcs = (uint32_t *)(base + *(uint32_t *)(dir + 0x1C));
  uint32_t *names = (uint32_t *)(base + *(uint32_t *)(dir + 0x20));
  uint16_t *ords = (uint16_t *)(base + *(uint32_t *)(dir + 0x24));

  for (uint32_t i = 0; i < num_names; ++i) {
    if (memllvm_hash((const char *)(base + names[i])) != hash)
      continue;

    uint32_t fn_rva = funcs[ords[i]];
    return memllvm_resolve_forwarder(base, fn_rva, exp_rva, exp_sz, depth);
  }

  return 0;
}

void *MemLLVM_GetApiAddress(uint32_t api_hash) {
  return memllvm_find(0, api_hash, 0);
}

void *MemLLVM_GetApiAddressByName(const char *name) {
  if (!name) return 0;
  return memllvm_find(0, memllvm_hash(name), 0);
}

void *MemLLVM_GetApiAddressByOrdinal(uint32_t dll_hash, uint32_t ordinal) {
  uint8_t *base = 0;
  if (dll_hash) {
    base = memllvm_find_module(dll_hash);
  }
  return memllvm_find_ordinal(base, ordinal, 0);
}

void *MemLLVM_GetProcAddress(void *module_base, const char *name_or_ordinal) {
  if (!module_base || !name_or_ordinal) return 0;
  if ((uintptr_t)name_or_ordinal <= 0xFFFF) {
    return memllvm_find_ordinal((uint8_t *)module_base, (uint32_t)(uintptr_t)name_or_ordinal, 0);
  }
  return memllvm_find((uint8_t *)module_base, memllvm_hash(name_or_ordinal), 0);
}
