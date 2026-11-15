// Regression tests for defects found by the independent review of Project 3.
//  1. run_merged must order messages globally by local time even when a feed's own timestamps
//     are out of order, so the export is strictly increasing and matches the reference pipeline.
//  2. `bench` must report stable counters for a feed that contains unparseable lines.
//  3. Sums and differences of untrusted int64 input must saturate instead of overflowing
//     (signed overflow is undefined behaviour; the sanitizer build catches it).
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "harness.hpp"
#include "md/bench.hpp"
#include "md/export.hpp"
#include "md/merge.hpp"
#include "md/saturate.hpp"
#include "md/stats.hpp"

#ifndef MD_SOURCE_DIR
#error "MD_SOURCE_DIR must be defined by the build"
#endif

using namespace md;

namespace {

constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();

Feed feed_of(Venue v, std::vector<std::string> lines) {
    Feed f;
    f.venue = v;
    f.lines = std::move(lines);
    return f;
}

std::string depth(std::int64_t r, std::int64_t seq, const std::string& px, const std::string& qty) {
    std::ostringstream os;
    os << "{\"r\":" << r << ",\"e\":\"depth\",\"E\":" << r / 1000 << ",\"first_seq\":" << seq << ",\"last_seq\":" << seq
       << ",\"b\":[[\"" << px << "\",\"" << qty << "\"]],\"a\":[]}";
    return os.str();
}

const char* kSnapshot =
    "{\"r\":1000000000,\"e\":\"snapshot\",\"E\":1000000,\"seq\":100,"
    "\"b\":[[\"100.00\",\"5.000\"]],\"a\":[[\"100.02\",\"5.000\"]]}";

std::string golden(const std::string& name) { return std::string(MD_SOURCE_DIR) + "/tests/golden/" + name; }

}  // namespace

// Defect 1: the second depth line carries an EARLIER local time than the first. Applied in local
// time order, seq 102 arrives before 101, which is a gap. The old lazy merge kept file order and
// emitted a timestamp that went backwards.
TEST(merge_orders_by_local_time_when_a_feed_is_out_of_order) {
    Feed a = feed_of(Venue::A, {kSnapshot, depth(2000000000, 101, "100.00", "6.000"),
                                depth(1500000000, 102, "100.00", "7.000")});
    Feed b = feed_of(Venue::B, {});
    std::vector<std::int64_t> ts;
    const MergeSummary s = run_merged(a, b, [&ts](const MergedQuote& q) { ts.push_back(q.local_ts_ns); });
    CHECK(!ts.empty());
    for (std::size_t i = 1; i < ts.size(); ++i) CHECK(ts[i] >= ts[i - 1]);
    CHECK_EQ(s.counters[0].gaps, 1);
}

TEST(export_is_strictly_increasing_when_a_feed_is_out_of_order) {
    Feed a = feed_of(Venue::A, {kSnapshot, depth(2000000000, 101, "100.00", "6.000"),
                                depth(1500000000, 102, "100.00", "7.000"),
                                depth(2500000000, 103, "100.00", "8.000")});
    Feed b = feed_of(Venue::B, {});
    std::ostringstream os;
    QuoteExporter ex(os);
    run_merged(a, b, [&ex](const MergedQuote& q) { ex.on_quote(q); });
    ex.finish();
    std::istringstream in(os.str());
    std::string line;
    std::getline(in, line);  // header
    long long prev = -1;
    int rows = 0;
    while (std::getline(in, line)) {
        const long long t = std::stoll(line.substr(0, line.find(',')));
        CHECK(t > prev);
        prev = t;
        ++rows;
    }
    CHECK_EQ(static_cast<long>(rows), static_cast<long>(ex.rows()));
}

// Defect 2: the end-to-end phase counts unparseable lines, the apply phases do not; a feed with a
// bad line must still be reported as stable.
TEST(bench_counters_are_stable_when_a_feed_has_parse_errors) {
    Feed a, b;
    std::string err;
    CHECK(load_feed(golden("venueA.jsonl"), Venue::A, a, err));
    CHECK(load_feed(golden("venueB.csv"), Venue::B, b, err));
    a.lines.insert(a.lines.begin() + 5, "garbage");
    BenchConfig cfg;
    cfg.reps = 2;
    cfg.warmup = 0;
    const BenchResult r = run_bench(a, b, cfg);
    CHECK_EQ(r.parse_errors, 1);
    CHECK(r.counters_stable);
}

TEST(bench_counters_are_stable_on_a_clean_feed) {
    Feed a, b;
    std::string err;
    CHECK(load_feed(golden("venueA.jsonl"), Venue::A, a, err));
    CHECK(load_feed(golden("venueB.csv"), Venue::B, b, err));
    BenchConfig cfg;
    cfg.reps = 2;
    cfg.warmup = 0;
    const BenchResult r = run_bench(a, b, cfg);
    CHECK_EQ(r.parse_errors, 0);
    CHECK(r.counters_stable);
}

// Defect 3: saturating arithmetic.
TEST(saturating_add_and_sub_clamp_instead_of_overflowing) {
    CHECK_EQ(sat_add(kMax, 1), kMax);
    CHECK_EQ(sat_add(kMax, kMax), kMax);
    CHECK_EQ(sat_add(kMin, -1), kMin);
    CHECK_EQ(sat_add(2, 3), 5);
    CHECK_EQ(sat_sub(kMin, 1), kMin);
    CHECK_EQ(sat_sub(kMax, -1), kMax);
    CHECK_EQ(sat_sub(0, kMin), kMax);
    CHECK_EQ(sat_sub(7, 3), 4);
}

TEST(latency_sum_saturates_on_extreme_samples) {
    LatencyStats l;
    l.add(kMax);
    l.add(kMax);
    l.add(kMax);
    const LatencySummary s = l.summary();
    CHECK_EQ(s.count, 3);
    CHECK_EQ(s.sum_ns, kMax);
    CHECK_EQ(s.max_ns, kMax);
}

TEST(merged_quantities_saturate_when_two_venues_quote_the_same_price) {
    Merger m;
    TopOfBook t;
    t.live = true;
    t.bid_px = 10000;
    t.bid_qty = kMax;
    t.ask_px = 10002;
    t.ask_qty = kMax;
    m.update(Venue::A, t, 1);
    m.update(Venue::B, t, 2);
    CHECK_EQ(m.current().bid_qty, kMax);
    CHECK_EQ(m.current().ask_qty, kMax);
    CHECK(m.current().bid_venues == 3);
}

TEST(export_skips_a_quote_whose_mid_would_overflow) {
    std::ostringstream os;
    QuoteExporter ex(os);
    MergedQuote q;
    q.live_venues = 1;
    q.bid_venues = 1;
    q.ask_venues = 1;
    q.bid_px = kMax - 2;
    q.bid_qty = 1;
    q.ask_px = kMax - 1;
    q.ask_qty = 1;
    q.local_ts_ns = 5000;
    ex.on_quote(q);
    ex.finish();
    CHECK_EQ(ex.rows(), 0);
    CHECK_EQ(os.str(), std::string("ts_us,bid,ask,mid\n"));
}

TEST(replay_latency_difference_saturates) {
    Feed a = feed_of(Venue::A, {"{\"r\":9223372036854775807,\"e\":\"trade\",\"E\":0,\"tid\":1,\"px\":\"100.01\","
                                "\"qty\":\"0.500\",\"side\":\"buy\"}",
                                "{\"r\":9223372036854775807,\"e\":\"trade\",\"E\":0,\"tid\":2,\"px\":\"100.01\","
                                "\"qty\":\"0.500\",\"side\":\"buy\"}"});
    Feed b = feed_of(Venue::B, {});
    const MergeSummary s = run_merged(a, b, nullptr);
    CHECK_EQ(s.latency[0].count, 2);
    CHECK_EQ(s.latency[0].sum_ns, kMax);
}
