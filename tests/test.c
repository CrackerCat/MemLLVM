#include <windows.h>

// MemLLVM Unified End-to-End Verification Sample
// Demonstrates both core capabilities:
// 1. StackStringPass: Constant string literals are unpacked to stack local arrays (0 .rdata)
// 2. ApiResolvePass: External Windows APIs are resolved dynamically via PEB/EAT ROR13 hash (0 IAT)

int main() {
    // 1. String literal testing (forces StackStringPass to unpack to stack, 0 .rdata)
    volatile const char *msg = "MemLLVM_Freestanding_Success";

    // 2. External API calls (forces ApiResolvePass to resolve via PEB, 0 IAT)
    char *buf = (char *)VirtualAlloc(NULL, 128, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buf) {
        return 1;
    }

    // Copy stack-unpacked string bytes to dynamic buffer
    int i = 0;
    while (msg[i] != '\0') {
        buf[i] = msg[i];
        i++;
    }
    buf[i] = '\0';

    // Verify written content matches
    if (buf[0] != 'M' || buf[6] != 'M' || buf[7] != '_') {
        VirtualFree(buf, 0, MEM_RELEASE);
        return 2;
    }

    VirtualFree(buf, 0, MEM_RELEASE);
    return 77; // Standard success code
}
