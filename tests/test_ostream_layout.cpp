#include <iostream>
#include <cstdio>

int main() {
    std::ios* pIos = static_cast<std::ios*>(&std::cout);
    char* base = (char*)pIos;
    printf("rdbuf offset in ios: %td\n", (char*)&pIos->rdbuf() - base);
    printf("tie offset in ios: %td\n", (char*)&pIos->tie() - base);
    return 0;
}
