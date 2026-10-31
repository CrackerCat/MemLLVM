#include <windows.h>
#include <vector>

struct Calculator {
    int baseValue;
    std::vector<int> numbers;

    Calculator() {
        baseValue = 1000;
        numbers.push_back(50);
        numbers.push_back(75);
    }

    int compute() {
        int total = baseValue;
        for (size_t i = 0; i < numbers.size(); ++i) {
            total += numbers[i];
        }
        return total;
    }
};

Calculator g_calc;

int main() {
    return g_calc.compute(); // Expected: 1125
}
