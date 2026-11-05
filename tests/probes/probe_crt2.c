/* probe_crt2 -- which CRT does the payload's stdout actually go through?
 *
 * probe_crt showed that in the real SCLoader fd 1 is still in its implicit
 * "console, not opened" state (_get_osfhandle returns -2) even though SCLoader
 * calls _dup2 right after SetStdHandle. SCLoader calls it through msvcrt.dll,
 * because it is built against VC-LTL. If the payload resolves its own _write /
 * _get_osfhandle to a *different* CRT -- ucrtbase.dll -- then the fd tables are
 * separate, and redirecting one does nothing for the other.
 *
 * This probe asks every CRT in the process the same question and prints the
 * answers side by side. If the numbers disagree, that is the whole bug: the
 * loader fixed a file-descriptor table nobody writes to.
 */

#include <windows.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <errno.h>

DWORD __stdcall GetTempPathA(DWORD nBufferLength, char *lpBuffer);

typedef intptr_t (__cdecl *get_osfhandle_fn)(int);
typedef int (__cdecl *open_osfhandle_fn)(intptr_t, int);
typedef int (__cdecl *dup2_fn)(int, int);

static char g_report[MAX_PATH] = "mllvm_probe_crt2.txt";
static HANDLE g_log = INVALID_HANDLE_VALUE;

static void Say(const char *s, int len)
{
    DWORD w = 0;
    if (g_log != INVALID_HANDLE_VALUE)
        WriteFile(g_log, s, (DWORD)len, &w, NULL);
}

static void SayFmt(const char *fmt, ...)
{
    char buf[1024];
    int n;
    va_list ap;
    va_start(ap, fmt);
    n = wvsprintfA(buf, fmt, ap);
    va_end(ap);
    if (n > 0)
        Say(buf, n);
}

/* The CRTs a payload might plausibly be linked against. Both of the real ones
 * are here plus the api-set forwarder, because a payload built with /MD may
 * have imported through the api-ms-win-crt-* umbrella instead. */
static const wchar_t *g_crts[] = {
    L"msvcrt.dll",
    L"ucrtbase.dll",
    L"api-ms-win-crt-runtime-l1-1-0.dll",
    L"api-ms-win-crt-stdio-l1-1-0.dll",
};

int main(void)
{
    char dir[MAX_PATH];
    char name[64];
    DWORD n;
    HANDLE hStdOut;
    int i;

    n = GetTempPathA(MAX_PATH, dir);
    wsprintfA(name, "mllvm_probe_crt2_%lu.txt", (unsigned long)GetCurrentProcessId());
    if (n > 0 && n < MAX_PATH) {
        lstrcpynA(g_report, dir, MAX_PATH);
        lstrcatA(g_report, name);
    } else {
        lstrcpynA(g_report, name, MAX_PATH);
    }

    g_log = CreateFileA(g_report, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_log == INVALID_HANDLE_VALUE)
        return 1;

    hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    SayFmt("=== MemLLVM multi-CRT probe ===\r\npid=%lu\r\n", (unsigned long)GetCurrentProcessId());
    SayFmt("GetStdHandle(out) = %p  filetype=%lu\r\n\r\n",
           (void *)hStdOut,
           hStdOut && hStdOut != INVALID_HANDLE_VALUE
               ? (unsigned long)GetFileType(hStdOut) : 0xFFFFFFFFul);

    SayFmt("%-38s %-6s %-18s %s\r\n", "module", "loaded", "fd1 os handle", "filetype");
    SayFmt("%-38s %-6s %-18s %s\r\n",
           "--------------------------------------", "------",
           "------------------", "--------");

    for (i = 0; i < (int)(sizeof(g_crts) / sizeof(g_crts[0])); ++i) {
        char narrow[128];
        HMODULE hMod;
        get_osfhandle_fn getf;
        intptr_t os;
        unsigned long type = 0xFFFFFFFF;
        int loaded;

        WideCharToMultiByte(CP_ACP, 0, g_crts[i], -1, narrow, sizeof(narrow), NULL, NULL);
        hMod = GetModuleHandleW(g_crts[i]);
        loaded = hMod != NULL;
        if (!loaded) {
            SayFmt("%-38s %-6s %-18s %s\r\n", narrow, "no", "-", "-");
            continue;
        }

        getf = (get_osfhandle_fn)GetProcAddress(hMod, "_get_osfhandle");
        if (!getf) {
            SayFmt("%-38s %-6s %-18s %s\r\n", narrow, "yes", "(no _get_osfhandle)", "-");
            continue;
        }
        os = getf(1);
        if (os != -1 && os != -2)
            type = (unsigned long)GetFileType((HANDLE)os);
        SayFmt("%-38s %-6s %-18s %s\r\n", narrow, "yes", "-", "");
        SayFmt("    fd1 = %p%s  filetype=%lu\r\n", (void *)os,
               os == -1 ? " (INVALID)" : (os == -2 ? " (NO_CONSOLE_FILENO)" : ""),
               type);
    }

    /* The payload's own view, through whatever CRT the MemLLVM import
     * resolution picked. This is the number that decides whether output lands. */
    SayFmt("\r\n-- payload's own CRT --\r\n");
    SayFmt("_get_osfhandle(1) = %p\r\n", (void *)_get_osfhandle(1));
    SayFmt("_get_osfhandle(2) = %p\r\n", (void *)_get_osfhandle(2));
    errno = 0;
    SayFmt("_write(1)         = %d  errno=%d\r\n", (int)_write(1, "", 0), errno);

    /* Can the payload repair it itself? If redirecting fd 1 through the CRT the
     * payload actually uses makes output appear, that is both the diagnosis and
     * the shape of the fix. */
    SayFmt("\r\n-- self-repair attempt --\r\n");
    for (i = 0; i < (int)(sizeof(g_crts) / sizeof(g_crts[0])); ++i) {
        char narrow[128];
        HMODULE hMod;
        get_osfhandle_fn getf;
        open_osfhandle_fn openf;
        dup2_fn dup2f;
        HANDLE mine = NULL;
        intptr_t os;
        int fd;
        int r1, r2;

        if (!hStdOut || hStdOut == INVALID_HANDLE_VALUE)
            break;
        hMod = GetModuleHandleW(g_crts[i]);
        if (!hMod)
            continue;
        getf = (get_osfhandle_fn)GetProcAddress(hMod, "_get_osfhandle");
        openf = (open_osfhandle_fn)GetProcAddress(hMod, "_open_osfhandle");
        dup2f = (dup2_fn)GetProcAddress(hMod, "_dup2");
        if (!getf || !openf || !dup2f)
            continue;

        os = getf(1);
        if (os != -1 && os != -2)
            continue;   /* this CRT is already redirected */

        /* Give each CRT its own handle so the ownership rules of
         * _open_osfhandle cannot close the pipe out from under the loader. */
        if (!DuplicateHandle(GetCurrentProcess(), hStdOut, GetCurrentProcess(),
                             &mine, 0, FALSE, DUPLICATE_SAME_ACCESS))
            continue;

        fd = openf((intptr_t)mine, _O_TEXT);
        if (fd == -1) {
            CloseHandle(mine);
            continue;
        }
        r1 = dup2f(fd, 1);
        r2 = dup2f(fd, 2);

        WideCharToMultiByte(CP_ACP, 0, g_crts[i], -1, narrow, sizeof(narrow), NULL, NULL);
        SayFmt("%-38s dup2(1)=%d dup2(2)=%d  fd1 now %p\r\n",
               narrow, r1, r2, (void *)getf(1));
    }

    SayFmt("\r\n=== done ===\r\n");
    CloseHandle(g_log);

    /* If the repair above worked, this line shows up in the loader's capture
     * area; if it does not, no CRT in this process was successfully rebound. */
    wprintf(L"[probe_crt2] stdio after repair\r\n");
    fflush(stdout);
    fflush(stderr);
    return 0;
}
