#include <future>

int compute(int x) {
    return x * 2;
}

int main() {
    std::future<int> f = std::async(std::launch::async, compute, 21);
    int res1 = f.get(); // 42

    std::promise<int> p;
    std::future<int> f2 = p.get_future();
    p.set_value(58);
    int res2 = f2.get(); // 58

    if (res1 == 42 && res2 == 58) {
        return 100;
    }
    return 1;
}
