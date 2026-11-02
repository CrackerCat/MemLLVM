/*
 * sim_scloader.c —— 等价复现「ShellcodeLoader 加载扁平镜像」的场景
 *
 * 和 probe_loader 的区别（这些差异就是 SCLoader 上跑不起来的原因）：
 *   1. stdout 被重定向到匿名管道，而不是直接继承控制台
 *   2. stdin 是一个空管道（loader 不提供输入），而不是控制台
 *   3. 用 CreateThread 默认栈执行，主线程 WaitForSingleObject
 *   4. 命令行没有给 payload 任何参数 -> mimikatz 收到 argc==0
 *
 * 用法：
 *   sim_scloader.exe <bin> [payload 参数...] [--timeout N] [--no-pipe]
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>

#define COMPILE_BASE 0x140001000ULL

static void RawErr(const char *s)
{
    DWORD w;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), s, (DWORD)lstrlenA(s), &w, NULL);
}

static void *g_base = NULL;
static unsigned long long g_size = 0;
static volatile LONG g_done = 0;
static int g_rc = -1;
static volatile LONG g_faults = 0;

/* 把栈上落在已知模块里的值打出来。扁平镜像没有 .pdata，做不了正经回溯，
   但「返回地址落在哪个模块的哪个偏移」足以定位卡在谁身上。 */
static void DumpStack(const char *tag, unsigned long long *sp, int depth)
{
    char buf[512];
    int i, printed = 0;
    for (i = 0; i < depth && printed < 14; i++) {
        unsigned long long v = sp[i];
        HMODULE h = NULL;
        char path[MAX_PATH];
        char *p;
        if (v < 0x10000)
            continue;
        /* payload 不是已加载模块，GetModuleHandleExA 会直接失败，
           所以必须单独认。否则最关键的那几帧恰好全被过滤掉。 */
        if (g_base && v >= (ULONG_PTR)g_base && v < (ULONG_PTR)g_base + g_size) {
            wsprintfA(buf, "  %s[%02d] %016I64X  <payload>+0x%IX\r\n", tag, i, v,
                      (size_t)(v - (ULONG_PTR)g_base));
            RawErr(buf);
            printed++;
            continue;
        }
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                (LPCSTR)(ULONG_PTR)v, &h) || !h)
            continue;
        path[0] = 0;
        GetModuleFileNameA(h, path, MAX_PATH);
        p = strrchr(path, '\\');
        wsprintfA(buf, "  %s[%02d] %016I64X  %s+0x%IX\r\n", tag, i, v,
                  p ? p + 1 : path, (size_t)(v - (unsigned long long)(ULONG_PTR)h));
        RawErr(buf);
        printed++;
    }
}

static LONG CALLBACK VectoredHandler(EXCEPTION_POINTERS *ep)
{
    CONTEXT *c = ep->ContextRecord;
    char buf[512];
    int i;
    DWORD code = ep->ExceptionRecord->ExceptionCode;

    /* 只报真正会杀死 payload 的异常。
       0xE06D7363 是 MSVC C++ EH 的魔术值，系统 DLL 内部会用它做正常的
       first-chance 抛掷（winscard/ucrtbase 都干过），随后自己接住。
       把它当 [FAULT] 打出来只会掩盖真正的问题。同理 0x406D1388（线程改名）、
       0xE0434352（CLR）都是良性信号。 */
    if (code == 0xE06D7363u || code == 0x406D1388u || code == 0xE0434352u ||
        (code & 0xC0000000u) == 0x40000000u)
        return EXCEPTION_CONTINUE_SEARCH;

    /* 抛点不在 payload 里也先打一条轻量标记（模块+偏移），
       因为「RIP 跑飞到 payload 外」本身就是关键症状。 */
    {
        ULONG_PTR ra = (ULONG_PTR)ep->ExceptionRecord->ExceptionAddress;
        if (!(g_base && ra >= (ULONG_PTR)g_base && ra < (ULONG_PTR)g_base + g_size)) {
            HMODULE h = NULL;
            char path[MAX_PATH], *p;
            path[0] = 0;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   (LPCSTR)(ULONG_PTR)ra, &h) && h) {
                GetModuleFileNameA(h, path, MAX_PATH);
                p = strrchr(path, '\\');
                wsprintfA(buf, "[FAULT-OUTSIDE] code=0x%08lX at %s+0x%IX (RIP outside payload)\r\n",
                          code, p ? p + 1 : path, (size_t)(ra - (ULONG_PTR)h));
            } else {
                wsprintfA(buf, "[FAULT-OUTSIDE] code=0x%08lX at %p (unknown module)\r\n",
                          code, (void *)ra);
            }
            RawErr(buf);
            return EXCEPTION_CONTINUE_SEARCH;
        }
    }

    if (InterlockedIncrement(&g_faults) > 6)
        return EXCEPTION_CONTINUE_SEARCH;
    wsprintfA(buf,
        "\r\n[FAULT] code=0x%08lX addr=%p\r\n"
        "  RIP=%016I64X RSP=%016I64X\r\n"
        "  RAX=%016I64X RCX=%016I64X RDX=%016I64X\r\n",
        ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress,
        c->Rip, c->Rsp, c->Rax, c->Rcx, c->Rdx);
    RawErr(buf);
    if (ep->ExceptionRecord->NumberParameters >= 2) {
        wsprintfA(buf, "  info0=%016I64X info1=%016I64X\r\n",
                  (unsigned long long)ep->ExceptionRecord->ExceptionInformation[0],
                  (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1]);
        RawErr(buf);
    }

    /* 栈扫描：把栈上落在已知模块里的返回地址打出来。
       扁平镜像没有 .pdata，没法做正经回溯，但「谁调了 RaiseException」
       看前几帧就够定位了。 */
    {
        unsigned long long *sp = (unsigned long long *)c->Rsp;
        int printed = 0;
        for (i = 0; i < 96 && printed < 10; i++) {
            unsigned long long v = sp[i];
            HMODULE h = NULL;
            char path[MAX_PATH];
            if (v < 0x10000)
                continue;
            if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    (LPCSTR)(ULONG_PTR)v, &h) || !h)
                continue;
            path[0] = 0;
            GetModuleFileNameA(h, path, MAX_PATH);
            {
                char *p = strrchr(path, '\\');
                wsprintfA(buf, "  [%02d] %016I64X  %s+0x%IX\r\n", i, v,
                          p ? p + 1 : path, (size_t)(v - (unsigned long long)(ULONG_PTR)h));
            }
            RawErr(buf);
            printed++;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static DWORD WINAPI PayloadThread(LPVOID p)
{
    (void)p;
    typedef int (*entry_t)(void);
    g_rc = ((entry_t)g_base)();
    InterlockedExchange(&g_done, 1);
    return 0;
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "x64\\mimikatz_mllvm.bin";
    int timeoutSec = 5, noPipe = 0, i;
    for (i = 2; i < argc; i++) {
        if (!lstrcmpA(argv[i], "--no-pipe")) noPipe = 1;
        if (!lstrcmpA(argv[i], "--timeout") && i + 1 < argc) timeoutSec = atoi(argv[++i]);
    }

    /* ---- 读 bin ---- */
    FILE *f = fopen(path, "rb");
    if (!f) { RawErr("[-] cannot open bin\n"); return 1; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *img = (unsigned char *)malloc((size_t)n);
    if (!img || fread(img, 1, (size_t)n, f) != (size_t)n) { fclose(f); return 1; }
    fclose(f);

    /* ---- 建立管道：stdout 捕获 + 空 stdin ---- */
    HANDLE outRd = NULL, outWr = NULL, inRd = NULL, inWr = NULL;
    if (!noPipe) {
        SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
        if (!CreatePipe(&outRd, &outWr, &sa, 1 << 20)) { RawErr("[-] pipe fail\n"); return 1; }
        if (!CreatePipe(&inRd, &inWr, &sa, 4096))     { RawErr("[-] pipe fail\n"); return 1; }
        SetHandleInformation(outRd, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(inWr,  HANDLE_FLAG_INHERIT, 0);

        /* 两条路都铺：Win32 的 GetStdHandle 和 CRT 的 fd 1/0。
           stderr 故意不动 —— 加载器自己的诊断必须留在控制台上，
           否则会跟 payload 的输出一起被关进管道，卡死时什么都看不到。 */
        SetStdHandle(STD_OUTPUT_HANDLE, outWr);
        SetStdHandle(STD_INPUT_HANDLE,  inRd);
        _dup2(_open_osfhandle((intptr_t)outWr, _O_WRONLY | _O_TEXT), 1);
        _dup2(_open_osfhandle((intptr_t)inRd,  _O_RDONLY | _O_TEXT), 0);

        char b[160];
        wsprintfA(b, "[*] stdout->pipe(1MB) stdin->empty pipe, timeout=%ds\n", timeoutSec);
        RawErr(b);
    } else {
        RawErr("[*] --no-pipe: 继承控制台\n");
    }

    /* ---- 分配并执行 ---- */
    void *base = VirtualAlloc(NULL, (SIZE_T)n + 0x10000,
                              MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!base) { RawErr("[-] VirtualAlloc fail\n"); return 1; }
    memcpy(base, img, (size_t)n);
    g_base = base;
    g_size = (unsigned long long)n;

    {
        char b[200];
        wsprintfA(b, "[*] base=%p size=%ld\n", base, n);
        RawErr(b);
    }

    AddVectoredExceptionHandler(1, VectoredHandler);

    HANDLE hThread = CreateThread(NULL, 0, PayloadThread, NULL, 0, NULL);
    if (!hThread) { RawErr("[-] CreateThread fail\n"); return 1; }

    DWORD waitMs = (DWORD)timeoutSec * 1000;
    DWORD wr = WaitForSingleObject(hThread, waitMs);

    /* 超时说明 payload 线程没返回。把它挂起，直接读 RIP —— 扁平镜像没有
       .pdata，没法回溯，但「最后停在哪条指令」通常就够判断卡在哪个调用了。 */
    if (wr == WAIT_TIMEOUT) {
        CONTEXT ctx;
        char b[400];
        HMODULE h = NULL;
        char path[MAX_PATH];
        SuspendThread(hThread);
        memset(&ctx, 0, sizeof(ctx));
        ctx.ContextFlags = CONTEXT_CONTROL;
        if (GetThreadContext(hThread, &ctx)) {
            path[0] = 0;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   (LPCSTR)(ULONG_PTR)ctx.Rip, &h) && h)
                GetModuleFileNameA(h, path, MAX_PATH);
            {
                char *p = strrchr(path, '\\');
                wsprintfA(b, "[*] 挂起线程 RIP=%016I64X (%s+0x%IX) RSP=%016I64X\r\n",
                          ctx.Rip, p ? p + 1 : path,
                          (size_t)(ctx.Rip - (unsigned long long)(ULONG_PTR)h), ctx.Rsp);
            }
            RawErr(b);
            if (g_base && ctx.Rip >= (ULONG_PTR)g_base &&
                ctx.Rip < (ULONG_PTR)g_base + (ULONG_PTR)n) {
                wsprintfA(b, "    -> 在 payload 内, 偏移 0x%IX\r\n",
                          (size_t)(ctx.Rip - (ULONG_PTR)g_base));
                RawErr(b);
            }
            DumpStack("S", (unsigned long long *)ctx.Rsp, 128);
        }
        ResumeThread(hThread);
    }

    {
        char b[160];
        wsprintfA(b, "[*] wait=%s done=%ld rc=%d\n",
                  wr == WAIT_TIMEOUT ? "TIMEOUT(卡死)" : "signaled", (long)g_done, g_rc);
        RawErr(b);
    }

    /* ---- 收管道里的输出 ---- */
    if (!noPipe) {
        /* 不能直接 CloseHandle(outWr) 然后 ReadFile：fd 1 是通过
           _open_osfhandle 接管的，句柄关掉后管道写端仍然被 fd 持有，
           ReadFile 会一直等下去。改用 PeekNamedPipe 轮询，有多少读多少。 */
        static char buf[1 << 20];
        DWORD total = 0;
        int idle = 0, spin;
        for (spin = 0; spin < 400 && idle < 6; spin++) {
            DWORD avail = 0;
            if (!PeekNamedPipe(outRd, NULL, 0, NULL, &avail, NULL))
                break;
            if (avail == 0) {
                idle++;
                Sleep(50);
                continue;
            }
            if (total + avail >= sizeof(buf) - 1)
                avail = (DWORD)(sizeof(buf) - 1 - total);
            if (avail == 0)
                break;
            DWORD got = 0;
            if (!ReadFile(outRd, buf + total, avail, &got, NULL) || got == 0)
                break;
            total += got;
            idle = 0;
        }
        buf[total] = '\0';

        char b[200];
        wsprintfA(b, "[*] 管道收到 %lu 字节\n", total);
        RawErr(b);
        RawErr("------------------------ PAYLOAD OUTPUT BEGIN ------------------------\n");
        if (total) {
            DWORD w;
            WriteFile(GetStdHandle(STD_ERROR_HANDLE), buf, total, &w, NULL);
        } else {
            RawErr("(空)\n");
        }
        RawErr("\n------------------------ PAYLOAD OUTPUT END --------------------------\n");
    }

    if (wr == WAIT_TIMEOUT) TerminateThread(hThread, 0);
    return 0;
}
