#include <chrono>

int main() {
    const auto* zone = std::chrono::current_zone();
    if (!zone) return 1;
    auto name = zone->name();
    return (name.size() > 0) ? 0 : 1;
}
