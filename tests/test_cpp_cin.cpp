#include <iostream>

int main() {
    char buf[16] = {0};
    std::cin >> buf;
    if (buf[0] == 'W' && buf[1] == 'o' && buf[2] == 'r' && buf[3] == 'l' && buf[4] == 'd') {
        std::cout << "Read verified: " << buf << "\n";
        return 55;
    }
    return 1;
}
