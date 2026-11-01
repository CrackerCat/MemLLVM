#include <windows.h>
#include <stdio.h>

static void *g_Base = NULL;
static size_t g_Size = 0;

static LONG WINAPI VectoredHandler(PEXCEPTION_POINTERS pEp) {
    DWORD code = pEp->ExceptionRecord->ExceptionCode;
    // Ignore internal C++ exceptions from Windows system DLLs and OutputDebugString
    if (code == 0xE06D7363 || code == 0x40010006) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    ULONG_PTR rip = (ULONG_PTR)pEp->ContextRecord->Rip;
    ULONG_PTR base = (ULONG_PTR)g_Base;
    HMODULE hMod = NULL;
    char modName[MAX_PATH] = "Unknown";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)rip, &hMod)) {
        GetModuleFileNameA(hMod, modName, MAX_PATH);
    }

    printf("\n[!] Exception caught! Code: 0x%08lX, Address: 0x%p (Module: %s)\n",
           code, (void*)rip, modName);
    if (rip >= base && rip < base + g_Size) {
        printf("    Inside shellcode! Offset: 0x%zX\n", rip - base);
    }
    if (code == 0xC0000005) {
        printf("    Access Violation: Type=%s, TargetAddress=0x%p\n",
               pEp->ExceptionRecord->ExceptionInformation[0] == 0 ? "READ" : "WRITE",
               (void*)pEp->ExceptionRecord->ExceptionInformation[1]);
    }
    ULONG_PTR *stack = (ULONG_PTR*)pEp->ContextRecord->Rsp;
    printf("    Call stack return addresses:\n");
    for (int i = 0; i < 32; i++) {
        ULONG_PTR addr = stack[i];
        if (addr >= base && addr < base + g_Size) {
            printf("      [%d] Shellcode Return: 0x%p (Offset: 0x%zX)\n", i, (void*)addr, addr - base);
        } else {
            HMODULE hM = NULL;
            char name[64] = "";
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &hM)) {
                GetModuleFileNameA(hM, name, 64);
                char *slash = strrchr(name, '\\');
                printf("      [%d] System Return: 0x%p (%s)\n", i, (void*)addr, slash ? slash + 1 : name);
            }
        }
    }
    if (code == 0xC0000005) {
        printf("    Access Violation: Type=%s, TargetAddress=0x%p\n",
               pEp->ExceptionRecord->ExceptionInformation[0] == 0 ? "READ" : "WRITE",
               (void*)pEp->ExceptionRecord->ExceptionInformation[1]);
    }
    if (rip >= base && rip < base + g_Size) {
        printf("    Inside shellcode! Offset: 0x%zX\n", rip - base);
    } else {
        printf("    Outside shellcode! (e.g. in system DLL / driver)\n");
        ULONG_PTR *stack = (ULONG_PTR*)pEp->ContextRecord->Rsp;
        for (int i = 0; i < 32; i++) {
            ULONG_PTR addr = stack[i];
            if (addr >= base && addr < base + g_Size) {
                printf("    -> Shellcode Caller Return Addr: 0x%p (Offset: 0x%zX)\n",
                       (void*)addr, addr - base);
                break;
            }
        }
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
