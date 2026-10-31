#include <windows.h>

struct Singleton {
    int counter;
    Singleton() : counter(100) {}
    int add(int v) {
        counter += v;
        return counter;
    }
};

Singleton& getSingleton() {
    static Singleton s;
    return s;
}

int main() {
    Singleton& s1 = getSingleton();
    s1.add(50);
    Singleton& s2 = getSingleton();
    s2.add(25);
    return s2.counter; // Expected: 175
}
