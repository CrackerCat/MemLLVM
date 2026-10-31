#include <windows.h>

// Initialized global (.data)
int g_counter = 500;
const char* g_msg = "Hello";

// Uninitialized global (.bss)
int g_buffer[64];

int main() {
    g_buffer[0] = 100;
    g_buffer[63] = 200;
    g_counter += g_buffer[0] + g_buffer[63]; // 500 + 100 + 200 = 800
    if (g_msg[0] == 'H') {
        g_counter += 88; // 800 + 88 = 888
    }
    return g_counter; // Expected: 888
}
