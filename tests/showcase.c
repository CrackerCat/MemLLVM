#include <windows.h>

// MemLLVM Feature Showcase Program
// Demonstrates:
// 1. Multiple string constants of various lengths (all inlined to stack via StackStringPass)
// 2. Multiple external Windows API calls across subsystems (all dynamically resolved via ApiResolvePass)

int main() {
    // -------------------------------------------------------------
    // 1. Various string constants (Short, Medium, Long)
    // -------------------------------------------------------------
    volatile const char *title = "MemLLVM Security Demo";
    volatile const char *shortStr = "Status: ACTIVE";
    volatile const char *intro = "=== MemLLVM Plugin Active ===";
    volatile const char *desc = 
        "Plugin Capabilities Verified:\n"
        " [x] Stack String Expansion (0 .rdata dependencies)\n"
        " [x] Dynamic PEB/EAT API Resolving (0 IAT dependencies)\n"
        " [x] Zero-CRT Freestanding Position-Independent Code\n";

    // -------------------------------------------------------------
    // 2. Various Windows API calls across subsystems
    // -------------------------------------------------------------
    // API Call 1: GetCurrentProcessId (kernel32)
    DWORD pid = GetCurrentProcessId();

    // API Call 2: GetTickCount (kernel32)
    DWORD ticks = GetTickCount();

    // API Call 3: VirtualAlloc (kernel32)
    char *buffer = (char *)VirtualAlloc(NULL, 1024, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buffer) {
        return 1;
    }

    // Combine strings into allocated buffer
    int pos = 0;

    #define APPEND(s) do { \
        int i = 0; \
        while ((s)[i] != '\0') { \
            buffer[pos++] = (s)[i++]; \
        } \
    } while(0)

    APPEND(intro);
    buffer[pos++] = '\n';
    buffer[pos++] = '\n';
    APPEND(desc);
    buffer[pos++] = '\n';
    APPEND(shortStr);
    buffer[pos++] = '\0';

    #undef APPEND

    // API Call 4: MessageBoxA (user32 - dynamically resolved via PEB with 0 IAT!)
    MessageBoxA(NULL, buffer, (LPCSTR)title, MB_OK | MB_ICONINFORMATION);

    // API Call 5: VirtualFree (kernel32)
    VirtualFree(buffer, 0, MEM_RELEASE);

    return 0;
}
