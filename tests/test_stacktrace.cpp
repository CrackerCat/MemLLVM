#include <stacktrace>
#include <string>

__declspec(noinline) int helper() {
    auto st = std::stacktrace::current();
    if (st.empty()) return 1;
    auto str = std::to_string(st);
    if (str.find("0> 0x") == std::string::npos) return 2;
    return 0;
}

int main() {
    return helper();
}
