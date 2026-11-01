#include "ltl_config.h"
#include <thread>

thread_local int g_my_tls = 10;

void add_to_tls(int x) {
    g_my_tls += x;
}

int read_tls() {
    return g_my_tls;
}

int g_thread_result = 0;

void thread_func(int val) {
    add_to_tls(val);
    g_thread_result = read_tls();
}

int main() {
    // 1. In main thread, increment tls
    add_to_tls(25); // g_my_tls should now be 35
    int main_tls_val = read_tls();

    // 2. Spawn a separate thread, its tls should start at 10 and become 10 + 50 = 60
    std::thread t(thread_func, 50);
    t.join();

    // 3. Verify main thread's tls remained untouched (still 35)
    int main_tls_val_after = read_tls();

    // Expected: main_tls_val (35) + g_thread_result (60) + main_tls_val_after (35) = 130
    return main_tls_val + g_thread_result + main_tls_val_after;
}
