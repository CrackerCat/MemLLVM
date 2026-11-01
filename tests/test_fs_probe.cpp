#include <windows.h>
#include <filesystem>

namespace fs = std::filesystem;

int main() {
    bool b = fs::exists("C:\\Windows");
    return b ? 123 : 456;
}
