#include <windows.h>
#include <stdio.h>
#include <time.h>

const char *findDllDeep(const char *api) {
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA("C:\\Windows\\System32\\*.dll", &fd);
    if (hFind == INVALID_HANDLE_VALUE) return "";

    static char found[MAX_PATH];
    found[0] = '\0';

    do {
        HMODULE h = LoadLibraryExA(fd.cFileName, NULL, DONT_RESOLVE_DLL_REFERENCES);
        if (h) {
            if (GetProcAddress(h, api)) {
                strncpy(found, fd.cFileName, MAX_PATH);
                FreeLibrary(h);
                break;
            }
            FreeLibrary(h);
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
    return found;
}

int main() {
    clock_t t0 = clock();
    const char *dll1 = findDllDeep("D3D11CreateDevice");
    printf("D3D11CreateDevice -> %s (time: %.3fs)\n", dll1, (double)(clock() - t0)/CLOCKS_PER_SEC);

    t0 = clock();
    const char *dll2 = findDllDeep("WSAStartup");
    printf("WSAStartup -> %s (time: %.3fs)\n", dll2, (double)(clock() - t0)/CLOCKS_PER_SEC);

    t0 = clock();
    const char *dll3 = findDllDeep("BCryptEncrypt");
    printf("BCryptEncrypt -> %s (time: %.3fs)\n", dll3, (double)(clock() - t0)/CLOCKS_PER_SEC);
    return 0;
}
