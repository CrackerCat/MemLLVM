#include <regex>
#include <string>

int main() {
    std::string s = "Hello 123 World";
    std::regex reg("\\d+");
    std::smatch m;
    if (std::regex_search(s, m, reg)) {
        return 88;
    }
    return 0;
}
