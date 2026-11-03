#include <cstdio>

struct Guard {
    ~Guard() {
        printf("[+] RAII local destructor executed during unwind!\n");
    }
};

void test_throw() {
    Guard g;
    printf("[+] Inside test_throw, throwing 12345...\n");
    throw 12345;
}

int main() {
    try {
        printf("[+] Entering try block...\n");
        test_throw();
        printf("[-] FAILED: Code after throw should not execute!\n");
        return 1;
    } catch (int e) {
        printf("[+] SUCCESS: Caught C++ exception: %d\n", e);
        if (e == 12345) {
            return 0;
        }
        return 2;
    } catch (...) {
        printf("[-] Caught unexpected exception!\n");
        return 3;
    }
    return 4;
}
