#include "ltl_config.h"
#include <filesystem>

namespace fs = std::filesystem;

int main() {
    fs::path p("C:\\Windows");
    if (!fs::exists(p)) return 1;
    if (!fs::is_directory(p)) return 2;

    fs::path np = p / "notepad.exe";
    if (!fs::exists(np)) return 3;
    
    auto sz = fs::file_size(np);
    if (sz == 0) return 4;

    return 200; // Success!
}
