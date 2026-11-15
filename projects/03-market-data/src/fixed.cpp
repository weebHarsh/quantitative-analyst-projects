#include "md/fixed.hpp"

#include <limits>

namespace md {

bool parse_uint(std::string_view s, std::int64_t& out) {
    if (s.empty()) return false;
    constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
    std::int64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        const std::int64_t d = c - '0';
        if (v > (kMax - d) / 10) return false;
        v = v * 10 + d;
    }
    out = v;
    return true;
}

bool parse_fixed(std::string_view s, int frac_digits, std::int64_t& out) {
    if (frac_digits < 0 || frac_digits > 18) return false;
    if (frac_digits == 0) return parse_uint(s, out);
    const std::size_t dot = s.find('.');
    if (dot == std::string_view::npos || dot == 0) return false;
    if (s.size() != dot + 1 + static_cast<std::size_t>(frac_digits)) return false;
    std::int64_t whole = 0, frac = 0;
    if (!parse_uint(s.substr(0, dot), whole)) return false;
    if (!parse_uint(s.substr(dot + 1), frac)) return false;
    std::int64_t scale = 1;
    for (int i = 0; i < frac_digits; ++i) scale *= 10;
    constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
    if (whole > (kMax - frac) / scale) return false;
    out = whole * scale + frac;
    return true;
}

std::string format_fixed(std::int64_t v, int frac_digits) {
    std::string sign;
    std::uint64_t u = static_cast<std::uint64_t>(v);
    if (v < 0) {
        sign = "-";
        u = 0 - u;
    }
    std::uint64_t scale = 1;
    for (int i = 0; i < frac_digits; ++i) scale *= 10;
    std::string out = sign + std::to_string(u / scale);
    if (frac_digits > 0) {
        std::string f = std::to_string(u % scale);
        out += '.';
        out.append(static_cast<std::size_t>(frac_digits) - f.size(), '0');
        out += f;
    }
    return out;
}

}  // namespace md
