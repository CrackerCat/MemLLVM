#include <atomic>
#include <semaphore>
#include <latch>
#include <barrier>
#include <thread>

int main() {
    // 1. std::atomic wait and notify
    std::atomic<int> flag{0};
    std::thread t1([&]() {
        flag.store(1);
        flag.notify_one();
    });
    flag.wait(0);
    t1.join();

    // 2. std::binary_semaphore
    std::binary_semaphore sem{0};
    std::thread t2([&]() {
        sem.release();
    });
    sem.acquire();
    t2.join();

    // 3. std::latch
    std::latch done{1};
    std::thread t3([&]() {
        done.count_down();
    });
    done.wait();
    t3.join();

    // 4. std::barrier
    std::barrier sync_point{2};
    std::thread t4([&]() {
        sync_point.arrive_and_wait();
    });
    sync_point.arrive_and_wait();
    t4.join();

    return 0;
}
