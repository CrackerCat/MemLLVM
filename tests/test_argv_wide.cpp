// Verification test: wmain promoted directly to entry without synthetic argc/argv overhead

extern "C" wchar_t *__stdcall GetCommandLineW(void);

#include <cstdio>
#include <cwchar>

int wmain()
{
    const wchar_t *p = GetCommandLineW();
    if (!p || wcslen(p) == 0) {
        printf("[-] GetCommandLineW returned empty\n");
        return 1;
    }
    printf("[+] SUCCESS: wmain directly promoted, command line verified: %ls\n", p);
    return 0;
}
