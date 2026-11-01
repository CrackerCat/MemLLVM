#include <windows.h>
struct Big {
    char data[256];
};
Big bss_var = { 1 };
void foo() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    WriteFile(h, "hi", 2, NULL, NULL);
    bss_var.data[0] = 2;
}
