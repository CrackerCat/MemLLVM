#include <locale>
#include <string>

int main() {
    std::locale loc_c("C");
    if (loc_c.name() != "C") return 1;

    std::locale loc_zh("zh-CN");
    if (loc_zh.name() != "zh-CN") return 2;

    std::locale loc_copy(loc_zh);
    if (loc_copy.name() != "zh-CN") return 3;

    return 0;
}
