#include <limits>

#include "harness.hpp"
#include "md/fixed.hpp"

using namespace md;

TEST(fixed_parse_price_and_qty) {
    std::int64_t v = -7;
    CHECK(parse_fixed("100.01", 2, v));
    CHECK_EQ(v, 10001);
    CHECK(parse_fixed("100.00", 2, v));
    CHECK_EQ(v, 10000);
    CHECK(parse_fixed("0.01", 2, v));
    CHECK_EQ(v, 1);
    CHECK(parse_fixed("1.250", 3, v));
    CHECK_EQ(v, 1250);
    CHECK(parse_fixed("0.000", 3, v));
    CHECK_EQ(v, 0);
    CHECK(parse_fixed("0.001", 3, v));
    CHECK_EQ(v, 1);
    CHECK(parse_fixed("12345.678", 3, v));
    CHECK_EQ(v, 12345678);
    CHECK(parse_fixed("007.10", 2, v));  // leading zeros are harmless
    CHECK_EQ(v, 710);
}

TEST(fixed_parse_rejects) {
    std::int64_t v = 42;
    CHECK(!parse_fixed("100.1", 2, v));
    CHECK(!parse_fixed("100.011", 2, v));
    CHECK(!parse_fixed("-1.00", 2, v));
    CHECK(!parse_fixed("+1.00", 2, v));
    CHECK(!parse_fixed("1e2", 2, v));
    CHECK(!parse_fixed("", 2, v));
    CHECK(!parse_fixed(".", 2, v));
    CHECK(!parse_fixed(".50", 2, v));
    CHECK(!parse_fixed("100.", 2, v));
    CHECK(!parse_fixed("100", 2, v));
    CHECK(!parse_fixed("1.2.3", 2, v));
    CHECK(!parse_fixed("1 .00", 2, v));
    CHECK(!parse_fixed("100.01", 3, v));
    CHECK(!parse_fixed("1.250", 2, v));
    CHECK_EQ(v, 42);  // untouched on failure
    // overflow: 92233720368547758.07 fits (INT64_MAX = 9223372036854775807), one more does not
    CHECK(parse_fixed("92233720368547758.07", 2, v));
    CHECK_EQ(v, std::numeric_limits<std::int64_t>::max());
    CHECK(!parse_fixed("92233720368547758.08", 2, v));
    CHECK(!parse_fixed("99999999999999999999.00", 2, v));
}

TEST(fixed_parse_uint) {
    std::int64_t v = 0;
    CHECK(parse_uint("0", v));
    CHECK_EQ(v, 0);
    CHECK(parse_uint("9223372036854775807", v));
    CHECK_EQ(v, std::numeric_limits<std::int64_t>::max());
    CHECK(!parse_uint("9223372036854775808", v));
    CHECK(!parse_uint("", v));
    CHECK(!parse_uint("-1", v));
    CHECK(!parse_uint("1a", v));
    CHECK(!parse_uint(" 1", v));
}

TEST(fixed_format_roundtrip) {
    CHECK_EQ(format_fixed(9997, 2), std::string("99.97"));
    CHECK_EQ(format_fixed(5, 2), std::string("0.05"));
    CHECK_EQ(format_fixed(0, 2), std::string("0.00"));
    CHECK_EQ(format_fixed(99975, 3), std::string("99.975"));
    CHECK_EQ(format_fixed(1250, 3), std::string("1.250"));
    CHECK_EQ(format_fixed(42, 0), std::string("42"));
    for (std::int64_t v : {0, 1, 99, 100, 9997, 10001, 123456789}) {
        std::int64_t back = -1;
        CHECK(parse_fixed(format_fixed(v, 2), 2, back));
        CHECK_EQ(back, v);
        CHECK(parse_fixed(format_fixed(v, 3), 3, back));
        CHECK_EQ(back, v);
    }
}

TEST(venue_b_px_e4_scaling) {
    // px_e4 / 100 = ticks (Venue B); covered end to end in the parser tests, here the arithmetic.
    std::int64_t v = 0;
    CHECK(parse_uint("1000100", v));
    CHECK_EQ(v / 100, 10001);
    CHECK_EQ(v % 100, 0);
}
