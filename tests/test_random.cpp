#include <random>

int main() {
    std::random_device rd;
    unsigned int x = rd();
    unsigned int y = rd();
    return (x != 0 && x != y) ? 0 : 1;
}
