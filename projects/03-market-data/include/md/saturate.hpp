#pragma once
// Saturating signed arithmetic. The parsers accept any value that fits in int64, so sums and
// differences of untrusted input must not overflow (signed overflow is undefined behaviour).
#include <cstdint>
#include <limits>

namespace md {

inline std::int64_t sat_add(std::int64_t a, std::int64_t b) {
    std::int64_t r = 0;
    if (__builtin_add_overflow(a, b, &r)) {
        return b > 0 ? std::numeric_limits<std::int64_t>::max() : std::numeric_limits<std::int64_t>::min();
    }
    return r;
}

inline std::int64_t sat_sub(std::int64_t a, std::int64_t b) {
    std::int64_t r = 0;
    if (__builtin_sub_overflow(a, b, &r)) {
        return b < 0 ? std::numeric_limits<std::int64_t>::max() : std::numeric_limits<std::int64_t>::min();
    }
    return r;
}

}  // namespace md
