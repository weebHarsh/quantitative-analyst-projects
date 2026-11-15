// Minimal test harness: TEST registry + CHECK / CHECK_EQ. No external dependencies.
#pragma once

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace mdtest {

struct TestCase {
    const char* name;
    void (*fn)();
};
inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}
struct Registrar {
    Registrar(const char* n, void (*f)()) { registry().push_back({n, f}); }
};
inline int& failures() {
    static int f = 0;
    return f;
}
inline long& checks() {
    static long c = 0;
    return c;
}

template <class T, class = void>
struct Streamable : std::false_type {};
template <class T>
struct Streamable<T, std::void_t<decltype(std::declval<std::ostream&>() << std::declval<const T&>())>>
    : std::true_type {};

template <class T>
std::string show(const T& v) {
    if constexpr (std::is_enum_v<T>) {
        return std::to_string(static_cast<long long>(v));
    } else if constexpr (std::is_same_v<T, std::uint8_t>) {
        return std::to_string(static_cast<unsigned>(v));
    } else if constexpr (Streamable<T>::value) {
        std::ostringstream os;
        os << v;
        return os.str();
    } else {
        return "<?>";
    }
}

}  // namespace mdtest

#define TEST(name)                                                         \
    static void test_##name();                                             \
    static ::mdtest::Registrar registrar_##name(#name, &test_##name);      \
    static void test_##name()

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        ++::mdtest::checks();                                                            \
        if (!(cond)) {                                                                   \
            ++::mdtest::failures();                                                      \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond "\n";   \
        }                                                                                \
    } while (0)

#define CHECK_EQ(a, b)                                                                    \
    do {                                                                                  \
        ++::mdtest::checks();                                                             \
        const auto& md_a_ = (a);                                                          \
        const auto& md_b_ = (b);                                                          \
        if (!(md_a_ == md_b_)) {                                                          \
            ++::mdtest::failures();                                                       \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK_EQ failed: " #a " == " #b \
                      << "  (" << ::mdtest::show(md_a_) << " vs " << ::mdtest::show(md_b_) << ")\n"; \
        }                                                                                 \
    } while (0)
