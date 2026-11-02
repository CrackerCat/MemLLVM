#include <cstdio>

extern int g_tu2_val;

int main() {
    printf("[+] In main: g_tu2_val = %d\n", g_tu2_val);
    if (g_tu2_val == 1) {
        printf("[+] SUCCESS: Non-entry TU constructor ran exactly once before main!\n");
        return 0;
    } else {
        printf("[-] FAILURE: Non-entry TU constructor ran %d times!\n", g_tu2_val);
        return 1;
    }
}

