#include <cstring>

#include "harness.hpp"

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0, failed_tests = 0;
    for (const auto& t : bttest::registry()) {
        if (filter != nullptr && std::strstr(t.name, filter) == nullptr) continue;
        const int before = bttest::failures();
        t.fn();
        ++ran;
        if (bttest::failures() != before) {
            ++failed_tests;
            std::cerr << "[FAIL] " << t.name << "\n";
        } else {
            std::cout << "[ ok ] " << t.name << "\n";
        }
    }
    std::cout << ran << " tests, " << bttest::checks() << " checks, " << failed_tests << " failed tests, "
              << bttest::failures() << " failed checks\n";
    return bttest::failures() == 0 && ran > 0 ? 0 : 1;
}
