#include <windows.h>

struct ElementDesc {
    const char* semanticName;
    int semanticIndex;
    int format;
    int inputSlot;
    int alignedByteOffset;
};

// Global struct containing both a pointer AND integer constants
ElementDesc g_elements[] = {
    {"POSITION", 0, 35, 0, 0},
    {"COLOR", 1, 42, 0, 16}
};

int main() {
    // Verify non-pointer integer fields were NOT wiped to 0
    if (g_elements[0].semanticIndex != 0) return 1;
    if (g_elements[0].format != 35) return 2;
    if (g_elements[1].semanticIndex != 1) return 3;
    if (g_elements[1].format != 42) return 4;
    if (g_elements[1].alignedByteOffset != 16) return 5;

    // Verify pointer was rebased and points to correct string
    if (g_elements[0].semanticName[0] != 'P') return 6;
    if (g_elements[1].semanticName[0] != 'C') return 7;

    return 777; // Expected: 777
}
