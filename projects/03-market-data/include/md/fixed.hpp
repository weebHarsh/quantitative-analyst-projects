// Integer-only decimal parsing and formatting.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace md {

// [0-9]+ only; overflow of int64 -> false. `out` is untouched on failure.
bool parse_uint(std::string_view s, std::int64_t& out);
// "<digits>.<exactly frac_digits digits>" -> value * 10^frac_digits. `out` untouched on failure.
bool parse_fixed(std::string_view s, int frac_digits, std::int64_t& out);
// v >= 0 expected; (9997, 2) -> "99.97". A negative v is formatted with a leading '-'.
std::string format_fixed(std::int64_t v, int frac_digits);

}  // namespace md
