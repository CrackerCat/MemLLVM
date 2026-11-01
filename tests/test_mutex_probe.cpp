#include <windows.h>
#include <mutex>

std::mutex g_mtx;
int g_val = 0;

int main() {
    g_mtx.lock();
    g_val += 42;
    g_mtx.unlock();
    return g_val;
}
