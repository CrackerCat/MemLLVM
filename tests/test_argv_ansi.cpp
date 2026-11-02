// Regression test for the ANSI entry path. __memllvm_get_ansi_argv had the same
// off-by-one as the wide variant (argc = numArgs - 1, argv[i] = argvW[i + 1]),
// so it needs its own coverage: the two implementations are separate code and
// fixing one does not prove the other.
//
// See test_argv_wide.cpp for why this matters. Same design: argv[0] must be the
// leading token of the process command line, which holds for both the native PE
// run and the flat image (where argv belongs to the host process).

extern "C" char *__stdcall GetCommandLineA(void);

#include <cstdio>
#include <cstring>

static int check(int argc, char **argv)
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

    const char *p = GetCommandLineA();
    if (p) {
        while (*p == ' ' || *p == '\t')
            ++p;
        if (*p == '"')
            ++p;
        size_t n = strlen(argv[0]);
        if (n == 0 || strncmp(p, argv[0], n) != 0) {
            printf("[-] argv[0] is not the leading token of the command line\n");
            bad = 1;
        }
    }

    return bad;
}

int main(int argc, char **argv)
{
    if (check(argc, argv))
        return 1;

    if (argc >= 2 && strcmp(argv[1], "alpha") == 0) {
        if (argc != 3 || strcmp(argv[2], "beta") != 0) {
            printf("[-] expected exactly (alpha, beta), got argc=%d\n", argc);
            return 1;
        }
        printf("[+] SUCCESS: argv forwarded as (program, alpha, beta), argc=%d\n", argc);
        return 0;
    }

    printf("[+] SUCCESS: argc=%d with argv[0] = program name and NULL terminator\n", argc);
    return 0;
}
