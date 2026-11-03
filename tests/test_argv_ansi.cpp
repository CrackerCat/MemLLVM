// Verification test: main promoted directly to entry without synthetic argc/argv overhead

extern "C" char *__stdcall GetCommandLineA(void);

#include <cstdio>
#include <cstring>

int main()
{
    const char *p = GetCommandLineA();
    if (!p || strlen(p) == 0) {
        printf("[-] GetCommandLineA returned empty\n");
        return 1;
    }
    printf("[+] SUCCESS: main directly promoted, command line verified: %s\n", p);
    return 0;
}
