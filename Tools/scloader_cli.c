/* scloader_cli -- command-line equivalent of Tools/SCLoader.exe.
 *
 * SCLoader is a GUI-subsystem app: at startup none of the standard handles
 * exist, and it redirects the payload's stdout/stderr through an anonymous
 * pipe before creating the payload thread. Reproducing that from a console
 * program needs more than "run the payload and read a pipe" -- the exact
 * sequence matters, because a payload can observe any of it:
 *
 *   CreatePipe(&rd, &wr, &sa{bInheritHandle=TRUE}, 0)
 *   SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0)
 *   saved_out = GetStdHandle(STD_OUTPUT_HANDLE)      // NULL in a GUI process
 *   saved_err = GetStdHandle(STD_ERROR_HANDLE)
 *   SetStdHandle(STD_OUTPUT_HANDLE, wr)
 *   SetStdHandle(STD_ERROR_HANDLE, wr)
 *   saved_fd1 = _dup(1);  saved_fd2 = _dup(2)
 *   fd_wr = _open_osfhandle(wr, _O_TEXT)
 *   _dup2(fd_wr, 1);  _dup2(fd_wr, 2)
 *   ... and the same dance for stdin, ending in _dup2(fd_rd, 0)
 *
 * The _dup2 half matters: it rebinds the CRT file descriptors, not just the
 * PEB's standard handles. A payload that reaches stdout through the CRT
 * (__acrt_iob_func / __stdio_common_*) instead of GetStdHandle sees only that.
 *
 * Because this tool is a console program, it starts by clearing the standard
 * handles itself so the payload sees the same NULL-initialised state a GUI
 * process would, and it keeps a private handle to the console so it can still
 * report results. --keep-stdhandles skips the clearing, which is useful for
 * telling "the payload needs a console" apart from "the redirect is broken".
 */

#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (*payload_entry_t)(void);

static HANDLE g_console = INVALID_HANDLE_VALUE;
static HANDLE g_log = INVALID_HANDLE_VALUE;
static HANDLE g_out_read = NULL;
static HANDLE g_out_write = NULL;
static HANDLE g_in_read = NULL;
static HANDLE g_in_write = NULL;
static int g_saved_fd[3] = {-1, -1, -1};
static int g_pipe_fd[3] = {-1, -1, -1};
static HANDLE g_saved_std[3] = {NULL, NULL, NULL};

/* Everything this tool prints goes through here. It cannot use stdout or
 * stderr: those are exactly what gets redirected to the payload's pipe. The
 * console handle is grabbed before the redirect, and a log file is kept as a
 * fallback for when there is no console at all (a pty, a service, CI). */
static void Out(const char *s)
{
    DWORD w = 0;
    DWORD len = (DWORD)lstrlenA(s);
    if (g_console != INVALID_HANDLE_VALUE)
        WriteFile(g_console, s, len, &w, NULL);
    if (g_log != INVALID_HANDLE_VALUE)
        WriteFile(g_log, s, len, &w, NULL);
}

static void OutFmt(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    wvsprintfA(buf, fmt, ap);
    va_end(ap);
    Out(buf);
}

static unsigned char *LoadPayload(const char *path, DWORD *out_size)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER size;
    unsigned char *buf;
    DWORD got = 0;
    if (h == INVALID_HANDLE_VALUE)
        return NULL;
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 || size.QuadPart > 0x40000000) {
        CloseHandle(h);
        return NULL;
    }
    buf = (unsigned char *)VirtualAlloc(NULL, (SIZE_T)size.QuadPart,
                                        MEM_COMMIT | MEM_RESERVE,
                                        PAGE_EXECUTE_READWRITE);
    if (!buf) {
        CloseHandle(h);
        return NULL;
    }
    if (!ReadFile(h, buf, (DWORD)size.QuadPart, &got, NULL) || got != (DWORD)size.QuadPart) {
        VirtualFree(buf, 0, MEM_RELEASE);
        CloseHandle(h);
        return NULL;
    }
    CloseHandle(h);
    *out_size = got;
    return buf;
}

/* Rebind one standard stream to a pipe, the way SCLoader does it. */
static BOOL RedirectStream(DWORD std_id, int crt_fd, HANDLE pipe_handle)
{
    g_saved_std[crt_fd] = GetStdHandle(std_id);
    SetStdHandle(std_id, pipe_handle);
    g_saved_fd[crt_fd] = _dup(crt_fd);
    g_pipe_fd[crt_fd] = _open_osfhandle((intptr_t)pipe_handle, _O_TEXT);
    if (g_pipe_fd[crt_fd] == -1)
        return FALSE;
    return _dup2(g_pipe_fd[crt_fd], crt_fd) == 0;
}

static void RestoreStreams(void)
{
    int i;
    for (i = 0; i < 3; ++i) {
        if (g_saved_fd[i] != -1) {
            _dup2(g_saved_fd[i], i);
            _close(g_saved_fd[i]);
            g_saved_fd[i] = -1;
        }
    }
    SetStdHandle(STD_OUTPUT_HANDLE, g_saved_std[1]);
    SetStdHandle(STD_ERROR_HANDLE, g_saved_std[2]);
    SetStdHandle(STD_INPUT_HANDLE, g_saved_std[0]);
}

static DWORD WINAPI PayloadThread(LPVOID param)
{
    ((payload_entry_t)param)();
    return 0;
}

int main(int argc, char **argv)
{
    const char *payload_path = NULL;
    const char *stdin_file = NULL;
    DWORD timeout_ms = 30000;
    int keep_stdhandles = 0;
    int restore_early = 0;
    int close_write_early = 0;
    int i;
    unsigned char *img;
    DWORD size = 0;
    SECURITY_ATTRIBUTES sa;
    HANDLE hThread;
    DWORD wait_result, exit_code = 0, total = 0;
    ULONGLONG t0, t1;

    for (i = 1; i < argc; ++i) {
        if (lstrcmpA(argv[i], "--keep-stdhandles") == 0)
            keep_stdhandles = 1;
        else if (lstrcmpA(argv[i], "--restore-early") == 0)
            restore_early = 1;
        else if (lstrcmpA(argv[i], "--close-write-early") == 0)
            close_write_early = 1;
        else if (lstrcmpA(argv[i], "--timeout") == 0 && i + 1 < argc)
            timeout_ms = (DWORD)atoi(argv[++i]);
        else if (lstrcmpA(argv[i], "--stdin-file") == 0 && i + 1 < argc)
            stdin_file = argv[++i];
        else if (argv[i][0] != '-')
            payload_path = argv[i];
        else {
            OutFmt("unknown option: %s\r\n", argv[i]);
            return 2;
        }
    }
    if (!payload_path) {
        Out("usage: scloader_cli <payload.bin> [--timeout ms] [--stdin-file path]\r\n"
            "                    [--keep-stdhandles] [--restore-early]\r\n"
            "                    [--close-write-early]\r\n"
            "\r\n"
            "  --keep-stdhandles    leave the console handles in place instead of\r\n"
            "                       clearing them (a GUI host starts with none)\r\n"
            "  --restore-early      put the standard handles back right after\r\n"
            "                       CreateThread instead of after the payload exits\r\n"
            "  --close-write-early  close the pipe write end right after\r\n"
            "                       CreateThread, leaving the payload a dead handle\r\n");
        return 2;
    }

    /* Keep private handles for reporting before anything is redirected. */
    g_console = CreateFileA("CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    g_log = CreateFileA("scloader_cli_out.txt", GENERIC_WRITE, FILE_SHARE_READ, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (!keep_stdhandles) {
        /* A GUI process has no standard handles at all; clear ours so the
         * payload starts from the same state. */
        SetStdHandle(STD_OUTPUT_HANDLE, NULL);
        SetStdHandle(STD_ERROR_HANDLE, NULL);
        SetStdHandle(STD_INPUT_HANDLE, NULL);
    }

    img = LoadPayload(payload_path, &size);
    if (!img) {
        OutFmt("[!] cannot read payload: %s\r\n", payload_path);
        return 1;
    }
    OutFmt("[*] payload   : %s\r\n", payload_path);
    OutFmt("[*] size      : %lu bytes\r\n", size);
    OutFmt("[*] base      : %p (RWX)\r\n", (void *)img);

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(&g_out_read, &g_out_write, &sa, 0)) {
        Out("[!] CreatePipe(stdout) failed\r\n");
        return 1;
    }
    SetHandleInformation(g_out_read, HANDLE_FLAG_INHERIT, 0);

    if (!CreatePipe(&g_in_read, &g_in_write, &sa, 0)) {
        Out("[!] CreatePipe(stdin) failed\r\n");
        return 1;
    }
    SetHandleInformation(g_in_read, HANDLE_FLAG_INHERIT, 0);

    if (!RedirectStream(STD_OUTPUT_HANDLE, 1, g_out_write) ||
        !RedirectStream(STD_ERROR_HANDLE, 2, g_out_write)) {
        Out("[!] redirecting stdout/stderr failed\r\n");
        return 1;
    }
    if (!RedirectStream(STD_INPUT_HANDLE, 0, g_in_read)) {
        Out("[!] redirecting stdin failed\r\n");
        return 1;
    }

    /* Optionally pre-load stdin. SCLoader leaves the pipe empty but open, so a
     * payload that reads stdin blocks forever; that is the behaviour being
     * reproduced unless --stdin-file is given. */
    if (stdin_file) {
        HANDLE hIn = CreateFileA(stdin_file, GENERIC_READ, FILE_SHARE_READ, NULL,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hIn != INVALID_HANDLE_VALUE) {
            char chunk[4096];
            DWORD got = 0;
            while (ReadFile(hIn, chunk, sizeof(chunk), &got, NULL) && got > 0) {
                DWORD wrote = 0;
                if (!WriteFile(g_in_write, chunk, got, &wrote, NULL))
                    break;
            }
            CloseHandle(hIn);
        }
        CloseHandle(g_in_write);
        g_in_write = NULL;
    }

    OutFmt("[*] std handles seen by the payload: out=%p err=%p in=%p\r\n",
           (void *)GetStdHandle(STD_OUTPUT_HANDLE),
           (void *)GetStdHandle(STD_ERROR_HANDLE),
           (void *)GetStdHandle(STD_INPUT_HANDLE));

    t0 = GetTickCount64();
    hThread = CreateThread(NULL, 0, PayloadThread, img, 0, NULL);
    if (!hThread) {
        Out("[!] CreateThread failed\r\n");
        return 1;
    }

    /* Two loader-side mistakes that both look identical from the outside: the
     * payload runs to completion, exits 0, and produces no output at all.
     * Keeping them behind flags makes that failure mode reproducible. */
    if (close_write_early) {
        _close(g_pipe_fd[1]);
        g_pipe_fd[1] = -1;
        CloseHandle(g_out_write);
        g_out_write = NULL;
        Out("[!] --close-write-early: pipe write end closed before the payload ran\r\n");
    }
    if (restore_early) {
        RestoreStreams();
        Out("[!] --restore-early: standard handles restored before the payload ran\r\n");
        OutFmt("    payload now sees out=%p err=%p\r\n",
               (void *)GetStdHandle(STD_OUTPUT_HANDLE),
               (void *)GetStdHandle(STD_ERROR_HANDLE));
    }

    wait_result = WaitForSingleObject(hThread, timeout_ms);
    GetExitCodeThread(hThread, &exit_code);
    t1 = GetTickCount64();

    /* The payload is done, so anything left in the pipe is all there is. */
    {
        char chunk[8192];
        Out("\r\n------------------------ [ PAYLOAD OUTPUT BEGIN ] ------------------------\r\n");
        for (;;) {
            DWORD avail = 0, got = 0;
            if (!PeekNamedPipe(g_out_read, NULL, 0, NULL, &avail, NULL) || avail == 0)
                break;
            if (!ReadFile(g_out_read, chunk, sizeof(chunk), &got, NULL) || got == 0)
                break;
            if (g_console != INVALID_HANDLE_VALUE) {
                DWORD w = 0;
                WriteFile(g_console, chunk, got, &w, NULL);
            }
            if (g_log != INVALID_HANDLE_VALUE) {
                DWORD w = 0;
                WriteFile(g_log, chunk, got, &w, NULL);
            }
            total += got;
        }
        Out("\r\n------------------------- [ PAYLOAD OUTPUT END ] -------------------------\r\n");
    }

    RestoreStreams();

    OutFmt("[*] wait      : %s\r\n",
           wait_result == WAIT_TIMEOUT ? "TIMEOUT" : (wait_result == WAIT_OBJECT_0 ? "signaled" : "other"));
    OutFmt("[*] exit code : 0x%08lX (%lu)\r\n", exit_code, exit_code);
    OutFmt("[*] elapsed   : %llu ms\r\n", (unsigned long long)(t1 - t0));
    OutFmt("[*] captured  : %lu bytes\r\n", total);

    if (g_console != INVALID_HANDLE_VALUE)
        CloseHandle(g_console);
    if (g_log != INVALID_HANDLE_VALUE)
        CloseHandle(g_log);
    return wait_result == WAIT_OBJECT_0 ? 0 : 1;
}
