// Export to Project 1's market schema (DESIGN.md section 9 / 12.6) and golden export.
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "harness.hpp"
#include "md/export.hpp"
#include "md/merge.hpp"
#include "md/parser_b.hpp"

#ifndef MD_SOURCE_DIR
#error "MD_SOURCE_DIR must be defined by the build"
#endif

using namespace md;

namespace {

std::string golden(const std::string& name) { return std::string(MD_SOURCE_DIR) + "/tests/golden/" + name; }

std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

Feed load(Venue v) {
    Feed f;
    std::string err;
    CHECK(load_feed(golden(v == Venue::A ? "venueA.jsonl" : "venueB.csv"), v, f, err));
    return f;
}

MergedQuote quote(std::int64_t ts_ns, std::int64_t bp, std::int64_t ap, std::uint8_t bv = 1,
                  std::uint8_t av = 1, std::uint8_t live = 1) {
    MergedQuote q;
    q.local_ts_ns = ts_ns;
    q.bid_px = bp;
    q.bid_qty = 1000;
    q.ask_px = ap;
    q.ask_qty = 1000;
    q.bid_venues = bv;
    q.ask_venues = av;
    q.live_venues = live;
    return q;
}

std::string run(const std::vector<MergedQuote>& qs, std::int64_t* rows = nullptr) {
    std::ostringstream os;
    QuoteExporter ex(os);
    for (const MergedQuote& q : qs) ex.on_quote(q);
    ex.finish();
    if (rows != nullptr) *rows = ex.rows();
    return os.str();
}

const char* kHeader = "ts_us,bid,ask,mid\n";

// Parses an export; checks the schema invariants and returns the ts_us column.
std::vector<std::int64_t> check_export_text(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    std::vector<std::int64_t> ts;
    CHECK(std::getline(in, line) && line == "ts_us,bid,ask,mid");
    while (std::getline(in, line)) {
        std::string_view f[5];
        CHECK_EQ(split_csv(line, f, 5), static_cast<std::size_t>(4));
        ts.push_back(std::stoll(std::string(f[0])));
        // bid/ask have exactly two decimals, mid exactly three
        CHECK(f[1].size() >= 4 && f[1][f[1].size() - 3] == '.');
        CHECK(f[2].size() >= 4 && f[2][f[2].size() - 3] == '.');
        CHECK(f[3].size() >= 5 && f[3][f[3].size() - 4] == '.');
        CHECK(std::stod(std::string(f[1])) < std::stod(std::string(f[2])));
        const double mid = std::stod(std::string(f[3]));
        CHECK(mid > std::stod(std::string(f[1])) - 1e-9 && mid < std::stod(std::string(f[2])) + 1e-9);
    }
    for (std::size_t i = 1; i < ts.size(); ++i) CHECK(ts[i] > ts[i - 1]);
    return ts;
}

}  // namespace

TEST(export_formats_prices_and_half_tick_mids) {
    CHECK_EQ(run({quote(1700000000000123456, 9997, 9998)}),
             std::string(kHeader) + "1700000000000123,99.97,99.98,99.975\n");
    CHECK_EQ(run({quote(5000, 10000, 10002)}), std::string(kHeader) + "5,100.00,100.02,100.010\n");
    CHECK_EQ(run({quote(5000, 5, 6)}), std::string(kHeader) + "5,0.05,0.06,0.055\n");
    CHECK_EQ(run({quote(5000, 100005, 100006)}), std::string(kHeader) + "5,1000.05,1000.06,1000.055\n");
}

TEST(export_ts_us_is_integer_division_of_ns) {
    CHECK_EQ(run({quote(1999, 100, 101)}), std::string(kHeader) + "1,1.00,1.01,1.005\n");
    CHECK_EQ(run({quote(2000, 100, 101)}), std::string(kHeader) + "2,1.00,1.01,1.005\n");
}

TEST(export_collapses_a_microsecond_to_its_last_quote) {
    std::int64_t rows = 0;
    const std::string out = run({quote(1000, 100, 102), quote(1400, 101, 103), quote(1999, 102, 104),
                                 quote(2500, 110, 112)},
                                &rows);
    CHECK_EQ(out, std::string(kHeader) + "1,1.02,1.04,1.030\n2,1.10,1.12,1.110\n");
    CHECK_EQ(rows, 2);
}

TEST(export_last_quote_of_the_microsecond_decides_the_filter) {
    // earlier fine quote, last quote of the microsecond is crossed: the microsecond exports nothing
    CHECK_EQ(run({quote(1000, 100, 102), quote(1500, 103, 102)}), std::string(kHeader));
    // earlier crossed, last fine: exported
    CHECK_EQ(run({quote(1000, 103, 102), quote(1500, 100, 102)}), std::string(kHeader) + "1,1.00,1.02,1.010\n");
    // earlier fine, last not live
    CHECK_EQ(run({quote(1000, 100, 102), quote(1500, 0, 0, 0, 0, 0)}), std::string(kHeader));
}

TEST(export_filters_locked_crossed_one_sided_and_not_live) {
    std::int64_t rows = -1;
    const std::string out = run({quote(1000, 100, 100),            // locked
                                 quote(2000, 101, 100),            // crossed
                                 quote(3000, 100, 0, 1, 0, 1),     // no ask
                                 quote(4000, 0, 102, 0, 1, 1),     // no bid
                                 quote(5000, 100, 102, 1, 1, 0),   // live mask empty
                                 quote(6000, 100, 101)},           // good
                                &rows);
    CHECK_EQ(out, std::string(kHeader) + "6,1.00,1.01,1.005\n");
    CHECK_EQ(rows, 1);
}

TEST(export_empty_and_finish_twice) {
    std::ostringstream os;
    QuoteExporter ex(os);
    ex.finish();
    ex.finish();
    CHECK_EQ(os.str(), std::string(kHeader));
    CHECK_EQ(ex.rows(), 0);
}

TEST(export_strictly_increasing_under_dense_quotes) {
    // many quotes per microsecond, random good/bad: output ts_us strictly increasing
    std::mt19937_64 rng(42);
    std::vector<MergedQuote> qs;
    std::int64_t t = 1700000000000000000;
    for (int i = 0; i < 5000; ++i) {
        t += static_cast<std::int64_t>(rng() % 700);
        const std::int64_t bp = 10000 + static_cast<std::int64_t>(rng() % 5);
        const std::int64_t ap = 10000 + static_cast<std::int64_t>(rng() % 5);
        qs.push_back(quote(t, bp, ap));
    }
    const auto ts = check_export_text(run(qs));
    CHECK(ts.size() > 100);
}

TEST(export_golden_matches_expected_export_csv_byte_for_byte) {
    std::ostringstream os;
    QuoteExporter ex(os);
    const MergeSummary s = run_merged(load(Venue::A), load(Venue::B), [&ex](const MergedQuote& q) { ex.on_quote(q); });
    ex.finish();
    const std::string want = slurp(golden("expected_export.csv"));
    CHECK(!want.empty());
    CHECK(os.str() == want);
    CHECK_EQ(ex.rows(), 38);  // expected_counters.csv: merge,export_rows
    CHECK_EQ(s.merged_quotes, 44);
}

TEST(export_faulted_golden_feed_is_strictly_increasing_and_never_crossed) {
    // The golden feeds carry drop, duplicate and crossed faults per venue.
    std::ostringstream os;
    QuoteExporter ex(os);
    const MergeSummary s = run_merged(load(Venue::A), load(Venue::B), [&ex](const MergedQuote& q) { ex.on_quote(q); });
    ex.finish();
    CHECK(s.counters[0].gaps + s.counters[1].gaps >= 2);
    CHECK(s.counters[0].crossed_detected + s.counters[1].crossed_detected >= 2);
    const auto ts = check_export_text(os.str());
    CHECK_EQ(static_cast<std::int64_t>(ts.size()), ex.rows());
    // exported rows are fewer than merged quotes: locked quotes and microsecond collapses drop some
    CHECK(ex.rows() < s.merged_quotes);
}

TEST(export_single_venue_runs_use_only_that_feed) {
    const Feed ea = [] { Feed f; f.venue = Venue::A; return f; }();
    const Feed eb = [] { Feed f; f.venue = Venue::B; return f; }();
    for (int which = 0; which < 2; ++which) {
        std::ostringstream os;
        QuoteExporter ex(os);
        const MergeSummary s = which == 0
            ? run_merged(load(Venue::A), eb, [&ex](const MergedQuote& q) { ex.on_quote(q); })
            : run_merged(ea, load(Venue::B), [&ex](const MergedQuote& q) { ex.on_quote(q); });
        ex.finish();
        CHECK(ex.rows() > 0);
        CHECK_EQ(s.merged_crossed, 0);
        CHECK_EQ(s.counters[which == 0 ? 1 : 0].messages, 0);
        check_export_text(os.str());
    }
}
