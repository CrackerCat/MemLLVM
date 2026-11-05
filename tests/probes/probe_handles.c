/* probe_handles -- diagnostic flat payload.
 *
 * Build it with the MemLLVM plugin, pack it with memllvm_pack.py, and load the
 * resulting .bin in whatever loader is being debugged. It records what the
 * payload can actually observe about the host's standard streams, because that
 * is the one thing a loader's own log cannot tell you.
 *
 * The report goes to a file, never to stdout: stdout is precisely what may be
 * broken, and the point is to find out whether it is.
 */

#include <windows.h>

/* Declared by hand so this compiles the same way whether the toolchain treats
 * the file as C or C++ (the plugin build path is a little unusual). */
wchar_t *__stdcall GetCommandLineW(void);
ULONGLONG __stdcall GetTickCount64(void);
DWORD __stdcall GetTempPathA(DWORD nBufferLength, char *lpBuffer);

/* The report cannot live in the working directory: a loader may not have one,
 * and it must not depend on C:\ being writable (it usually is not). */
static char g_report[MAX_PATH] = "mllvm_probe.txt";

static void BuildReportPath(void)
{
    char dir[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, dir);
    if (n > 0 && n < MAX_PATH) {
        lstrcpynA(g_report, dir, MAX_PATH);
        lstrcatA(g_report, "mllvm_probe.txt");
    }
}

static void Append(HANDLE f, const char *s, int len)
{
    DWORD w = 0;
    WriteFile(f, s, (DWORD)len, &w, NULL);
}

int main()
{
    HANDLE hLog;
    char buf[1024];
    int n;
    HANDLE hs[3];
    const char *names[3] = { "stdin ", "stdout", "stderr" };
    int i;

    BuildReportPath();

    hLog = CreateFileA(g_report, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hLog == INVALID_HANDLE_VALUE) {
        DWORD w = 0;
        const char msg[] = "MemLLVM probe: cannot create report file\r\n";
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        if (h && h != INVALID_HANDLE_VALUE)
            WriteFile(h, msg, (DWORD)(sizeof(msg) - 1), &w, NULL);
        return 1;
    }

    Append(hLog, "=== MemLLVM std-handle probe ===\r\n", 33);

    n = wsprintfA(buf, "pid=%lu tick=%lu\r\n",
                  (unsigned long)GetCurrentProcessId(),
                  (unsigned long)GetTickCount64());
    Append(hLog, buf, n);

    hs[0] = GetStdHandle(STD_INPUT_HANDLE);
    hs[1] = GetStdHandle(STD_OUTPUT_HANDLE);
    hs[2] = GetStdHandle(STD_ERROR_HANDLE);

    for (i = 0; i < 3; ++i) {
        unsigned long type = 0xFFFFFFFF;
        if (hs[i] && hs[i] != INVALID_HANDLE_VALUE)
            type = (unsigned long)GetFileType(hs[i]);
        n = wsprintfA(buf, "%s: handle=%p filetype=%lu%s\r\n",
                      names[i], (void *)hs[i], type,
                      (!hs[i] || hs[i] == INVALID_HANDLE_VALUE) ? "  <-- INVALID" : "");
        Append(hLog, buf, n);
    }

    /* Does a raw write actually land? A handle can look fine and still be a
     * stale value left behind by a loader that closed it early. */
    for (i = 1; i <= 2; ++i) {
        DWORD wrote = 0;
        BOOL ok;
        DWORD err;
        SetLastError(0);
        ok = (hs[i] && hs[i] != INVALID_HANDLE_VALUE)
                 ? WriteFile(hs[i], "probe\n", 6, &wrote, NULL)
                 : FALSE;
        err = GetLastError();
        n = wsprintfA(buf, "WriteFile(%s): ok=%d wrote=%lu err=%lu\r\n",
                      names[i], (int)ok, (unsigned long)wrote, (unsigned long)err);
        Append(hLog, buf, n);
    }

    /* A console that is still attached changes what a payload may safely do
     * (mimikatz, for one, only enters its interactive loop when stdin is a
     * console). */
    {
        DWORD mode = 0;
        n = wsprintfA(buf, "console window=%p  GetConsoleMode(in)=%d\r\n",
                      (void *)GetConsoleWindow(),
                      (int)GetConsoleMode(hs[0], &mode));
        Append(hLog, buf, n);
    }

    {
        const wchar_t *cl = GetCommandLineW();
        if (cl) {
            n = WideCharToMultiByte(CP_UTF8, 0, cl, -1, buf, sizeof(buf) - 1, NULL, NULL);
            Append(hLog, "cmdline: ", 9);
            if (n > 0)
                Append(hLog, buf, n - 1);
            Append(hLog, "\r\n", 2);
        }
    }

    Append(hLog, "=== done ===\r\n", 13);
    CloseHandle(hLog);

    /* Also try the payload's normal output path, so the loader's own log shows
     * something when the redirect works at all. The report path goes out too,
     * because that is how the file gets found afterwards. */
    {
        DWORD w = 0;
        char msg[512];
        HANDLE h = hs[1];
        int len = wsprintfA(msg, "MemLLVM probe report: %s\r\n", g_report);
        if (h && h != INVALID_HANDLE_VALUE)
            WriteFile(h, msg, (DWORD)len, &w, NULL);
    }
    return 0;
}
