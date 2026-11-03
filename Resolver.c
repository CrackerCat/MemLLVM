/*
 * Resolver.c — Ultra-compact MemLLVM_GetApiAddress with Direct Target Module Forwarder Resolution
 *
 * Direct PEB module list traversal + ROR13 export symbol lookup.
 * Directly resolves export forwarders within the loaded target module,
 * preventing recursive module scans and redundant LoadLibrary calls.
 */

typedef unsigned int uint32_t;
typedef unsigned short uint16_t;
typedef unsigned char uint8_t;

#define HASH_LOADLIBRARYA 0xEC0E4E8E
#define MAX_FORWARD_DEPTH 4

static inline uint32_t memllvm_ror13(const char *s) {
  uint32_t h = 0;
  while (*s) {
    h = (h >> 13) | (h << 19);
    h += (uint8_t)*s++;
  }
  return h;
}

static void *memllvm_resolve(uint32_t hash, int depth);

static void *memllvm_lookup_module(uint8_t *base, uint32_t hash, int depth) {
  if (!base || *(uint16_t *)base != 0x5A4D)
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
    const char *name = (const char *)(base + names[i]);
    if (memllvm_ror13(name) != hash)
      continue;

    uint32_t fn_rva = funcs[ords[i]];

    // Check if export RVA falls within export directory bounds (Forwarder)
    if ((uint32_t)(fn_rva - exp_rva) < exp_sz) {
      if (depth >= MAX_FORWARD_DEPTH)
        return 0;

      const char *fwd = (const char *)(base + fn_rva);
      char buf[48];
      int n = 0;
      while (fwd[n] && fwd[n] != '.' && n < 36) {
        buf[n] = fwd[n];
        n++;
      }
      if (fwd[n] != '.')
        return 0;
      buf[n] = '\0';

      void *(*load_lib)(const char *) =
          (void *(*)(const char *))memllvm_resolve(HASH_LOADLIBRARYA, depth + 1);
      if (!load_lib)
        return 0;

      uint8_t *target_base = (uint8_t *)load_lib(buf);
      if (!target_base) {
        buf[n] = '.';
        buf[n + 1] = 'd';
        buf[n + 2] = 'l';
        buf[n + 3] = 'l';
        buf[n + 4] = '\0';
        target_base = (uint8_t *)load_lib(buf);
        if (!target_base)
          return 0;
      }

      if (fwd[n + 1] == '#') {
        uint32_t ord = 0;
        const char *p = fwd + n + 2;
        while (*p >= '0' && *p <= '9')
          ord = ord * 10 + (*p++ - '0');
        uint32_t t_pe = *(uint32_t *)(target_base + 0x3C);
        uint32_t t_exp = *(uint32_t *)(target_base + t_pe + 0x88);
        if (!t_exp)
          return 0;
        uint32_t t_base_ord = *(uint32_t *)(target_base + t_exp + 0x10);
        uint32_t *t_funcs =
            (uint32_t *)(target_base + *(uint32_t *)(target_base + t_exp + 0x1C));
        return target_base + t_funcs[ord - t_base_ord];
      }

      // Directly resolve inside the loaded target module
      return memllvm_lookup_module(target_base, memllvm_ror13(fwd + n + 1), depth + 1);
    }

    return base + fn_rva;
  }

  return 0;
}

static void *memllvm_resolve(uint32_t hash, int depth) {
  if (depth > MAX_FORWARD_DEPTH)
    return 0;

  uint8_t *peb;
  __asm__ volatile("movq %%gs:0x60, %0" : "=r"(peb));
  if (!peb)
    return 0;

  uint8_t *ldr = *(uint8_t **)(peb + 0x18);
  if (!ldr)
    return 0;

  uint8_t *head = ldr + 0x10;
  uint8_t *cur = *(uint8_t **)head;

  while (cur && cur != head) {
    uint8_t *base = *(uint8_t **)(cur + 0x30);
    cur = *(uint8_t **)cur;
    void *addr = memllvm_lookup_module(base, hash, depth);
    if (addr)
      return addr;
  }

  return 0;
}

void *MemLLVM_GetApiAddress(uint32_t ror13_hash) {
  return memllvm_resolve(ror13_hash, 0);
}
