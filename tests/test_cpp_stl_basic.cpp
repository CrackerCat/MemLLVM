#include <windows.h>
#include <vector>

int main() {
    std::vector<int> v;
    v.push_back(100);
    v.push_back(200);
    v.push_back(300);
    
    int sum = 0;
    for (size_t i = 0; i < v.size(); ++i) {
        sum += v[i];
    }
    return sum; // Expected: 600
}
