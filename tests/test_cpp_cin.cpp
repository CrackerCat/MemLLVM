#include <iostream>

int main() {
    std::cout << "Please enter text: ";
    char buf[64] = {0};
    std::cin >> buf;
    std::cout << "\n[ECHO] Received input: " << buf << "\n";
    if (buf[0] == 'W' && buf[1] == 'o' && buf[2] == 'r' && buf[3] == 'l' && buf[4] == 'd') {
        std::cout << "[SUCCESS] Read verified: " << buf << "\n";
        return 55;
    }
    return 1;
}
