#include "ltl_config.h"
#include <thread>
#include <mutex>

int g_result = 0;
std::mutex g_thrd_mtx;

void worker(int val) {
    std::lock_guard<std::mutex> lock(g_thrd_mtx);
    g_result += val;
}

int main() {
    std::thread t1(worker, 150);
    std::thread t2(worker, 250);
    t1.join();
    t2.join();
    return g_result; // Expected: 400
}
