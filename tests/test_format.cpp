#include <format>
#include <string>

int main() {
    std::string s = std::format("Float: {:.2f}, Int: {}", 3.14159, 100);
    return (s == "Float: 3.14, Int: 100") ? 0 : 1;
}
