/* probe_crt -- which layer of the output path survives a loader's redirect?
 *
 * probe_handles answers "does the payload see a usable standard handle". This
 * one answers the follow-up question, which is the one that actually decides
 * whether a payload prints anything: does the C runtime agree with the handle?
 *
 * A loader can rebind the PEB's standard handles and stop there:
 *
 *     SetStdHandle(STD_OUTPUT_HANDLE, pipe_write);
 *
 * That is enough for WriteFile(GetStdHandle(...)), which is what probe_handles
 * checks, so the loader looks healthy. But printf and wprintf do not read the
 * PEB. They go through fd 1, and fd 1 still points at whatever the CRT bound
 * at startup. Unless the loader also does
 *
 *     fd = _open_osfhandle(pipe_write, _O_TEXT);  _dup2(fd, 1);
 *
 * every CRT write lands on a dead descriptor and disappears -- silently, with
 * no error the loader can report.
 *
 * So the decisive line in this report is "fd1 vs stdhandle": if the two differ,
 * the loader only did half the job, and any payload that prints through the
 * CRT (mimikatz, for one) will look like it produced nothing at all.
 */

#include <windows.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <errno.h>

/* Declared by hand so this compiles the same way whether the toolchain treats
 * the file as C or C++ (the plugin build path is a little unusual). */
wchar_t *__stdcall GetCommandLineW(void);
DWORD __stdcall GetTempPathA(DWORD nBufferLength, char *lpBuffer);

/* The pid goes into the name. Two loaders are compared against each other and
 * a leftover report from the previous run is indistinguishable from a fresh
 * one otherwise -- which is exactly the kind of mistake this probe exists to
 * rule out, not commit. */
static char g_report[MAX_PATH] = "mllvm_probe_crt.txt";
static HANDLE g_log = INVALID_HANDLE_VALUE;

static void BuildReportPath(void)
{
    char dir[MAX_PATH];
    char name[64];
    DWORD n = GetTempPathA(MAX_PATH, dir);
    wsprintfA(name, "mllvm_probe_crt_%lu.txt", (unsigned long)GetCurrentProcessId());
    if (n > 0 && n < MAX_PATH) {
        lstrcpynA(g_report, dir, MAX_PATH);
        lstrcatA(g_report, name);
    } else {
        lstrcpynA(g_report, name, MAX_PATH);
    }
}

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

/* Report one attempt in a fixed shape so the lines can be compared across
 * loaders without reading them one by one. */
static void Report(const char *what, int ret, int err)
{
    SayFmt("%-22s ret=%-6d errno=%d\r\n", what, ret, err);
}

int main(void)
{
    HANDLE hStdOut, hStdErr;
    intptr_t fd1_os, fd2_os;

    BuildReportPath();
    g_log = CreateFileA(g_report, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_log == INVALID_HANDLE_VALUE)
        return 1;

    SayFmt("=== MemLLVM CRT-path probe ===\r\npid=%lu\r\n",
           (unsigned long)GetCurrentProcessId());

    hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    hStdErr = GetStdHandle(STD_ERROR_HANDLE);
    fd1_os = _get_osfhandle(1);
    fd2_os = _get_osfhandle(2);

    SayFmt("\r\n-- identity --\r\n");
    SayFmt("GetStdHandle(out)  = %p\r\n", (void *)hStdOut);
    SayFmt("GetStdHandle(err)  = %p\r\n", (void *)hStdErr);
    SayFmt("_get_osfhandle(1)  = %p\r\n", (void *)fd1_os);
    SayFmt("_get_osfhandle(2)  = %p\r\n", (void *)fd2_os);
    SayFmt("_fileno(stdout)    = %d\r\n", _fileno(stdout));
    SayFmt("_fileno(stderr)    = %d\r\n", _fileno(stderr));
    SayFmt("_fileno(stdin)     = %d\r\n", _fileno(stdin));

    /* The whole point of the probe. Two values can differ and still be right:
     * a loader that gives the CRT its own DuplicateHandle copy of the pipe is
     * doing the correct thing, because _open_osfhandle takes ownership and two
     * CRTs sharing one handle would close it out from under each other. So
     * compare by what the handles point at, not by their numeric value. */
    SayFmt("\r\n-- verdict --\r\n");
    {
        int fd1_ok = 0, fd2_ok = 0;

        if ((HANDLE)fd1_os == hStdOut) {
            fd1_ok = 1;
            SayFmt("fd1 vs stdhandle   : MATCH (same handle)\r\n");
        } else if (fd1_os != -1 && fd1_os != -2 && hStdOut &&
                   hStdOut != INVALID_HANDLE_VALUE &&
                   GetFileType((HANDLE)fd1_os) == FILE_TYPE_PIPE &&
                   GetFileType(hStdOut) == FILE_TYPE_PIPE) {
            fd1_ok = 1;
            SayFmt("fd1 vs stdhandle   : OK (same pipe, duplicated handle %p vs %p)\r\n",
                   (void *)fd1_os, (void *)hStdOut);
        } else {
            SayFmt("fd1 vs stdhandle   : MISMATCH (redirect stops at the PEB)\r\n");
        }

        if ((HANDLE)fd2_os == hStdErr) {
            fd2_ok = 1;
            SayFmt("fd2 vs stdhandle   : MATCH (same handle)\r\n");
        } else if (fd2_os != -1 && fd2_os != -2 && hStdErr &&
                   hStdErr != INVALID_HANDLE_VALUE &&
                   GetFileType((HANDLE)fd2_os) == FILE_TYPE_PIPE &&
                   GetFileType(hStdErr) == FILE_TYPE_PIPE) {
            fd2_ok = 1;
            SayFmt("fd2 vs stdhandle   : OK (same pipe, duplicated handle %p vs %p)\r\n",
                   (void *)fd2_os, (void *)hStdErr);
        } else {
            SayFmt("fd2 vs stdhandle   : MISMATCH (redirect stops at the PEB)\r\n");
        }

        SayFmt("RESULT             : %s\r\n",
               (fd1_ok && fd2_ok) ? "PASS - CRT writes reach the pipe"
                                  : "FAIL - CRT writes are discarded");
    }

    /* Now walk up the stack: raw handle, low-level fd, buffered stdio, and the
     * wide-character path mimikatz actually uses. Each is a separate place the
     * output can be lost. */
    SayFmt("\r\n-- write paths --\r\n");

    if (hStdOut && hStdOut != INVALID_HANDLE_VALUE) {
        DWORD wrote = 0;
        BOOL ok;
        SetLastError(0);
        ok = WriteFile(hStdOut, "[WriteFile]\n", 12, &wrote, NULL);
        SayFmt("%-22s ok=%d wrote=%lu winerr=%lu\r\n", "WriteFile(stdhandle)",
               (int)ok, (unsigned long)wrote, (unsigned long)GetLastError());
    } else {
        SayFmt("%-22s skipped, no stdhandle\r\n", "WriteFile(stdhandle)");
    }

    errno = 0;
    Report("_write(1)", (int)_write(1, "[_write]\n", 9), errno);

    errno = 0;
    {
        int wr, fl, e1;
        wr = fputs("[fputs]\n", stdout);
        e1 = errno;
        errno = 0;
        fl = fflush(stdout);
        SayFmt("%-22s fputs=%d fflush=%d errno=%d\r\n", "fputs+fflush(stdout)",
               wr, fl, e1 ? e1 : errno);
    }

    errno = 0;
    {
        int wr, fl, e1;
        wr = fprintf(stdout, "[fprintf]\n");
        e1 = errno;
        errno = 0;
        fl = fflush(stdout);
        SayFmt("%-22s fprintf=%d fflush=%d errno=%d\r\n", "fprintf+fflush",
               wr, fl, e1 ? e1 : errno);
    }

    /* mimikatz's kprintf is vwprintf + fflush(stdout). If only one line in this
     * report is empty in the loader's output, it is this one. */
    errno = 0;
    {
        int wr, fl, e1;
        wr = wprintf(L"[wprintf]\n");
        e1 = errno;
        errno = 0;
        fl = fflush(stdout);
        SayFmt("%-22s wprintf=%d fflush=%d errno=%d\r\n", "wprintf+fflush",
               wr, fl, e1 ? e1 : errno);
    }

    errno = 0;
    {
        int wr, fl, e1;
        wr = fputws(L"[fputws]\n", stdout);
        e1 = errno;
        errno = 0;
        fl = fflush(stdout);
        SayFmt("%-22s fputws=%d fflush=%d errno=%d\r\n", "fputws+fflush",
               wr, fl, e1 ? e1 : errno);
    }

    errno = 0;
    {
        int wr, fl, e1;
        wr = fputs("[fputs-stderr]\n", stderr);
        e1 = errno;
        errno = 0;
        fl = fflush(stderr);
        SayFmt("%-22s fputs=%d fflush=%d errno=%d\r\n", "fputs+fflush(stderr)",
               wr, fl, e1 ? e1 : errno);
    }

    /* Whether the stream is still considered interactive changes what a payload
     * is willing to do: mimikatz only keeps a prompt when it thinks it has a
     * console. */
    SayFmt("\r\n-- interactivity --\r\n");
    SayFmt("isatty(1)          = %d\r\n", _isatty(1));
    SayFmt("isatty(0)          = %d\r\n", _isatty(0));

    SayFmt("\r\n=== done ===\r\n");
    CloseHandle(g_log);

    /* One last raw write, so the loader's own capture area shows something when
     * the redirect works at all, and the report path is discoverable from it. */
    if (hStdOut && hStdOut != INVALID_HANDLE_VALUE) {
        DWORD w = 0;
        char msg[512];
        int len = wsprintfA(msg, "MemLLVM CRT probe report: %s\r\n", g_report);
        WriteFile(hStdOut, msg, (DWORD)len, &w, NULL);
    }
    return 0;
}
