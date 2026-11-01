#include "ltl_config.h"
#include <mutex>

std::mutex g_mtx;
int g_shared_counter = 0;

int main() {
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        g_shared_counter += 100;
    }
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        g_shared_counter += 250;
    }
    return g_shared_counter; // Expected: 350
}
