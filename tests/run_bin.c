#include <windows.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
    const char *binPath = (argc > 1) ? argv[1] : "gui_window.bin";

    FILE *f = fopen(binPath, "rb");
    if (!f) {
        printf("[-] Failed to open '%s'\n", binPath);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    size_t sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    void *buf = VirtualAlloc(NULL, sz, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!buf) {
        printf("[-] VirtualAlloc failed\n");
        fclose(f);
        return 1;
    }

    fread(buf, 1, sz, f);
    fclose(f);

    printf("===============================================================\n");
    printf("  MemLLVM Flat Machine Code Runner\n");
    printf("===============================================================\n");
    printf("[+] Target Binary: %s (%zu bytes)\n", binPath, sz);
    printf("[+] Allocated Memory: 0x%p (PAGE_EXECUTE_READWRITE)\n", buf);
    printf("[+] Executing raw machine code from offset 0x00...\n\n");

    typedef int (*ShellcodeEntry)();
    ShellcodeEntry entry = (ShellcodeEntry)buf;

    int exitCode = entry();

    printf("\n[+] Machine code execution completed cleanly. Return code: %d\n", exitCode);
    VirtualFree(buf, 0, MEM_RELEASE);
    return exitCode;
}
