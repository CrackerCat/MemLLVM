#include <windows.h>
#include <stdio.h>

static void *g_Base = NULL;
static size_t g_Size = 0;

static LONG WINAPI VectoredHandler(PEXCEPTION_POINTERS pEp) {
    ULONG_PTR rip = (ULONG_PTR)pEp->ContextRecord->Rip;
    ULONG_PTR base = (ULONG_PTR)g_Base;
    if (rip >= base && rip < base + g_Size) {
        printf("\n[-] Crash inside shellcode! Address: 0x%p (Offset: 0x%zX)\n",
               (void*)rip, rip - base);
        printf("[-] Exception Code: 0x%08lX\n", pEp->ExceptionRecord->ExceptionCode);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    const char *binPath = (argc > 1) ? argv[1] : "D:\\MemLLVM\\tests\\gui_window_cpp.bin";

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

    g_Base = buf;
    g_Size = sz;
    AddVectoredExceptionHandler(1, VectoredHandler);

    FlushInstructionCache(GetCurrentProcess(), buf, sz);

    printf("===============================================================\n");
    printf("  MemLLVM Flat Machine Code Runner\n");
    printf("===============================================================\n");
    printf("[+] Target Binary: %s (%zu bytes)\n", binPath, sz);
    printf("[+] Base Memory: 0x%p\n", buf);
    printf("[+] Executing raw machine code from offset 0x00...\n");

    typedef int (*ShellcodeEntry)();
    ShellcodeEntry entry = (ShellcodeEntry)buf;

    int exitCode = entry();
    printf("\n[+] Machine code execution completed cleanly! Return code: %d\n", exitCode);
    VirtualFree(buf, 0, MEM_RELEASE);
    return exitCode;
}
