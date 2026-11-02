// Regression test: MemLLVM must hand the entry function a standard CRT
// argc/argv, i.e. argv[0] is the *program name* and argc counts it.
//
// This exists because the argv reconstruction used to strip the program name
// (argc = numArgs - 1, argv = &argvW[1]). Everything still compiled and every
// other test still passed, but a program invoked as "app.exe version" saw
// argc == 1 and argv[0] == "version": it treated its first real argument as the
// executable path, so its command loop started at argv[1] and ran nothing.
//
// The check is "argv[0] must be the leading token of the process command line".
// That holds in both ways this test runs:
//   * native PE  -- argv[0] is the exe path, plus an exact positional check
//   * flat image -- argv belongs to the host process (python.exe), so only the
//                   leading-token invariant applies
// Under the off-by-one argv[0] is the *second* token and both modes fail.

extern "C" wchar_t *__stdcall GetCommandLineW(void);

#include <cstdio>
#include <cstring>
#include <cwchar>

static int check(int argc, wchar_t **argv)
{
    int bad = 0;

    if (argc < 1) {
        printf("[-] argc=%d, expected >= 1\n", argc);
        return 1;
    }
    if (!argv) {
        printf("[-] argv is NULL\n");
        return 1;
    }

    for (int i = 0; i < argc; i++) {
        if (!argv[i]) {
            printf("[-] argv[%d] is NULL\n", i);
            bad = 1;
        }
    }
    if (argv[argc] != nullptr) {
        printf("[-] argv[argc] is not NULL-terminated\n");
        bad = 1;
    }
    if (bad)
        return bad;

    // argv[0] must be the first token of the raw command line. Skip leading
    // blanks and an opening quote, which CommandLineToArgvW strips.
    const wchar_t *p = GetCommandLineW();
    if (p) {
        while (*p == L' ' || *p == L'\t')
            ++p;
        if (*p == L'"')
            ++p;
        size_t n = wcslen(argv[0]);
        if (n == 0 || wcsncmp(p, argv[0], n) != 0) {
            printf("[-] argv[0] is not the leading token of the command line\n");
            bad = 1;
        }
    }

    return bad;
}

int wmain(int argc, wchar_t **argv)
{
    if (check(argc, argv))
        return 1;

    // Native PE mode passes a known pair; verify the exact positions. Skipped
    // for the flat image, whose argv belongs to the host process.
    if (argc >= 2 && wcscmp(argv[1], L"alpha") == 0) {
        if (argc != 3 || wcscmp(argv[2], L"beta") != 0) {
            printf("[-] expected exactly (alpha, beta), got argc=%d\n", argc);
            return 1;
        }
        printf("[+] SUCCESS: argv forwarded as (program, alpha, beta), argc=%d\n", argc);
        return 0;
    }

    printf("[+] SUCCESS: argc=%d with argv[0] = program name and NULL terminator\n", argc);
    return 0;
}
