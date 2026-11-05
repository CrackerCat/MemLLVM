#include <execution>
#include <algorithm>
#include <vector>

int main() {
    std::vector<int> v = {42, 17, 99, 1, 8, 23};
    std::sort(std::execution::par, v.begin(), v.end());
    return (v[0] == 1 && v[5] == 99) ? 0 : 1;
}
