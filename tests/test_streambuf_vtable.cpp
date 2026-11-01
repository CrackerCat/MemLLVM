#include <iostream>
#include <cstdio>

class MyBuf : public std::streambuf {
public:
    int overflow(int ch) override { return ch; }
    std::streamsize xsputn(const char* s, std::streamsize n) override { return n; }
    int sync() override { return 0; }
};

int main() {
    MyBuf buf;
    void** vtable = *(void***)&buf;
    printf("streambuf vtable pointer: %p\n", vtable);
    for (int i = 0; i < 16; ++i) {
        printf("  vtable[%d] (offset +%02d): %p\n", i, i * 8, vtable[i]);
    }
    return 0;
}
