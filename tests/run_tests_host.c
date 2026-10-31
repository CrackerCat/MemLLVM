#include <windows.h>
#include <stdio.h>

typedef int (*RunAllTestsFn)();

int main() {
    HMODULE hMod = LoadLibraryA("D:\\MemLLVM\\build\\MemLLVM\\Release\\MemLLVM.dll");
    if (!hMod) { printf("Failed to load MemLLVM.dll: %lu\n", GetLastError()); return 1; }
    RunAllTestsFn fn = (RunAllTestsFn)GetProcAddress(hMod, "RunAllTests");
    if (!fn) { printf("Failed to find RunAllTests: %lu\n", GetLastError()); return 1; }
    int res = fn();
    printf("RunAllTests returned: %d\n", res);
    return res;
}
