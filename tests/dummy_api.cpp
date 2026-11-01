#include <windows.h>
int bss_var;
void foo() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    WriteFile(h, "hi", 2, NULL, NULL);
    bss_var = 1;
}
