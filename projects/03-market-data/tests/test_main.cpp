#include <cstring>

#include "harness.hpp"

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0, failed_tests = 0;
    for (const auto& t : mdtest::registry()) {
        if (filter != nullptr && std::strstr(t.name, filter) == nullptr) continue;
        const int before = mdtest::failures();
        t.fn();
        ++ran;
        if (mdtest::failures() != before) {
            ++failed_tests;
            std::cerr << "[FAIL] " << t.name << "\n";
        } else {
            std::cout << "[ ok ] " << t.name << "\n";
        }
    }
    std::cout << ran << " tests, " << mdtest::checks() << " checks, " << failed_tests
              << " failed tests, " << mdtest::failures() << " failed checks\n";
    return mdtest::failures() == 0 && ran > 0 ? 0 : 1;
}
