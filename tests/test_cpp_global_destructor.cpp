#include <windows.h>

static int g_trace = 0;

struct Stage2 {
    ~Stage2() {
        g_trace += 20; // second dtor
        ExitProcess(g_trace); // Expected exit code: 31
    }
};

struct Stage1 {
    ~Stage1() {
        g_trace += 10; // first dtor (reverse order: Stage1 constructed after Stage2, so destructed first)
    }
};

Stage2 g_s2; // constructed 1st -> destructed 2nd
Stage1 g_s1; // constructed 2nd -> destructed 1st

int main() {
    g_trace = 1;
    // When main returns, destructors execute in reverse LIFO order:
    // 1. ~Stage1(): g_trace becomes 1 + 10 = 11
    // 2. ~Stage2(): g_trace becomes 11 + 20 = 31, then ExitProcess(31)
    return 0;
}
