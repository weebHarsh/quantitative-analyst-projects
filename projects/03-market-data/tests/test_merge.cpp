// Cross-venue merge tests (DESIGN.md section 8 / 12.5 / 12.6): hand-built feeds, Merger units,
// latency statistics through the merge, and the golden merged output.
#include <cmath>
#include <fstream>
#include <map>
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

Feed feed_of(Venue v, std::vector<std::string> lines) {
    Feed f;
    f.venue = v;
    f.lines = std::move(lines);
    return f;
}

TopOfBook top(std::int64_t bp, std::int64_t bq, std::int64_t ap, std::int64_t aq, bool live = true) {
    TopOfBook t;
    t.live = live;
    t.bid_px = bp;
    t.bid_qty = bq;
    t.ask_px = ap;
    t.ask_qty = aq;
    return t;
}

// Hand-built scenario (all values synthetic).
//  A: snapshot bid 100.00x1 / ask 100.03x2 at r=5.0ms; depth ask 100.02x5 at r=6.0ms.
//  B: snapshot (id 20) bid 100.01x3 / ask 100.04x4, all rows at r=5.0ms (same time as A's snapshot);
//     id 21 new bid 100.02x6 at 7ms; id 22 new bid 100.03x7 at 8ms; id 24 (prev 23: a gap) at 9ms.
Feed scenario_a() {
    return feed_of(Venue::A,
                   {"{\"r\":5000000,\"e\":\"snapshot\",\"E\":4000,\"seq\":10,\"b\":[[\"100.00\",\"1.000\"]],"
                    "\"a\":[[\"100.03\",\"2.000\"]]}",
                    "{\"r\":6000000,\"e\":\"depth\",\"E\":4500,\"first_seq\":11,\"last_seq\":11,\"b\":[],"
                    "\"a\":[[\"100.02\",\"5.000\"]]}"});
}
Feed scenario_b() {
    return feed_of(Venue::B, {"5000000,SB,4500000,20,,,,,2", "5000000,SL,4500000,20,,,1,1000100,3000",
                              "5000000,SL,4500000,20,,,2,1000400,4000", "5000000,SE,4500000,20,,,,,",
                              "7000000,U,6000000,21,20,N,1,1000200,6000",
                              "8000000,U,7700000,22,21,N,1,1000300,7000",
                              "9000000,U,9200000,24,23,C,1,1000300,1000"});
}

}  // namespace

TEST(merger_equal_prices_aggregate_and_set_both_bits) {
    Merger m;
    CHECK(m.update(Venue::A, top(10000, 1000, 10002, 2000), 100));
    CHECK(m.update(Venue::B, top(10000, 3000, 10002, 4000), 200));
    const MergedQuote& q = m.current();
    CHECK_EQ(q.local_ts_ns, 200);
    CHECK_EQ(q.bid_px, 10000);
    CHECK_EQ(q.bid_qty, 4000);
    CHECK_EQ(q.bid_venues, static_cast<std::uint8_t>(3));
    CHECK_EQ(q.ask_px, 10002);
    CHECK_EQ(q.ask_qty, 6000);
    CHECK_EQ(q.ask_venues, static_cast<std::uint8_t>(3));
    CHECK_EQ(q.live_venues, static_cast<std::uint8_t>(3));
}

TEST(merger_better_price_wins_only_that_venue_counts) {
    Merger m;
    m.update(Venue::A, top(10000, 1000, 10003, 2000), 1);
    CHECK(m.update(Venue::B, top(10001, 3000, 10004, 4000), 2));
    const MergedQuote& q = m.current();
    CHECK_EQ(q.bid_px, 10001);
    CHECK_EQ(q.bid_qty, 3000);
    CHECK_EQ(q.bid_venues, static_cast<std::uint8_t>(2));  // B
    CHECK_EQ(q.ask_px, 10003);
    CHECK_EQ(q.ask_qty, 2000);
    CHECK_EQ(q.ask_venues, static_cast<std::uint8_t>(1));  // A
    // B improves its ask below A's, and A improves its bid above B's: the roles swap sides
    CHECK(m.update(Venue::B, top(10001, 3000, 10002, 9), 3));
    CHECK_EQ(m.current().ask_venues, static_cast<std::uint8_t>(2));
    CHECK(m.update(Venue::A, top(10002, 7, 10003, 2000), 4));
    CHECK_EQ(m.current().bid_px, 10002);
    CHECK_EQ(m.current().bid_venues, static_cast<std::uint8_t>(1));
}

TEST(merger_stale_venue_is_excluded) {
    Merger m;
    m.update(Venue::A, top(10000, 1000, 10003, 2000), 1);
    m.update(Venue::B, top(10002, 3000, 10004, 4000), 2);
    CHECK_EQ(m.current().bid_px, 10002);
    // B goes stale (non-live top, all zeros): merged falls back to A alone
    CHECK(m.update(Venue::B, top(0, 0, 0, 0, false), 3));
    const MergedQuote& q = m.current();
    CHECK_EQ(q.bid_px, 10000);
    CHECK_EQ(q.bid_venues, static_cast<std::uint8_t>(1));
    CHECK_EQ(q.live_venues, static_cast<std::uint8_t>(1));
    // a non-live top that still carries prices is ignored too
    CHECK(!m.update(Venue::B, top(10009, 5, 10010, 5, false), 4));
    // no live venue: everything zero
    CHECK(m.update(Venue::A, top(0, 0, 0, 0, false), 5));
    CHECK_EQ(q.live_venues, static_cast<std::uint8_t>(0));
    CHECK_EQ(q.bid_px, 0);
    CHECK_EQ(q.ask_venues, static_cast<std::uint8_t>(0));
}

TEST(merger_no_change_emits_nothing_and_keeps_timestamp) {
    Merger m;
    CHECK(!m.update(Venue::A, top(0, 0, 0, 0, false), 1));  // initial all-zero state: nothing to emit
    CHECK(m.update(Venue::A, top(10000, 1000, 10001, 2000), 2));
    CHECK(!m.update(Venue::A, top(10000, 1000, 10001, 2000), 3));
    CHECK(!m.update(Venue::B, top(0, 0, 0, 0, false), 4));  // B stays not live
    CHECK_EQ(m.current().local_ts_ns, 2);
    // a quantity change alone is a change
    CHECK(m.update(Venue::A, top(10000, 1001, 10001, 2000), 5));
    CHECK_EQ(m.current().local_ts_ns, 5);
}

TEST(merger_one_sided_live_venue) {
    Merger m;
    CHECK(m.update(Venue::A, top(10000, 1000, 0, 0), 1));  // live, only a bid
    CHECK_EQ(m.current().bid_venues, static_cast<std::uint8_t>(1));
    CHECK_EQ(m.current().ask_venues, static_cast<std::uint8_t>(0));
    CHECK_EQ(m.current().live_venues, static_cast<std::uint8_t>(1));
}

TEST(merge_scenario_tie_stale_locked_crossed) {
    std::vector<MergedQuote> qs;
    const MergeSummary s = run_merged(scenario_a(), scenario_b(), [&qs](const MergedQuote& q) { qs.push_back(q); });
    CHECK_EQ(qs.size(), static_cast<std::size_t>(6));
    if (qs.size() != 6) return;
    CHECK_EQ(s.merged_quotes, 6);

    // q0, q1: A and B snapshots share local time 5 ms; A goes first, so A alone is emitted first.
    CHECK_EQ(qs[0].local_ts_ns, 5000000);
    CHECK_EQ(qs[0].live_venues, static_cast<std::uint8_t>(1));
    CHECK_EQ(qs[0].bid_px, 10000);
    CHECK_EQ(qs[0].bid_qty, 1000);
    CHECK_EQ(qs[0].ask_px, 10003);
    CHECK_EQ(qs[1].local_ts_ns, 5000000);
    CHECK_EQ(qs[1].live_venues, static_cast<std::uint8_t>(3));
    CHECK_EQ(qs[1].bid_px, 10001);  // B's better bid
    CHECK_EQ(qs[1].bid_venues, static_cast<std::uint8_t>(2));
    CHECK_EQ(qs[1].ask_px, 10003);  // A's better ask
    CHECK_EQ(qs[1].ask_venues, static_cast<std::uint8_t>(1));

    // q2: A improves its ask to 100.02
    CHECK_EQ(qs[2].local_ts_ns, 6000000);
    CHECK_EQ(qs[2].ask_px, 10002);
    CHECK_EQ(qs[2].ask_qty, 5000);

    // q3: B bids 100.02 == A's ask: locked merged quote
    CHECK_EQ(qs[3].bid_px, 10002);
    CHECK_EQ(qs[3].ask_px, 10002);
    // q4: B bids 100.03 > A's ask 100.02: crossed merged quote
    CHECK_EQ(qs[4].bid_px, 10003);
    CHECK_EQ(qs[4].ask_px, 10002);
    CHECK_EQ(s.merged_locked, 1);
    CHECK_EQ(s.merged_crossed, 1);

    // q5: B has a gap (id 24 after 22) and goes stale: only A remains
    CHECK_EQ(qs[5].local_ts_ns, 9000000);
    CHECK_EQ(qs[5].live_venues, static_cast<std::uint8_t>(1));
    CHECK_EQ(qs[5].bid_px, 10000);
    CHECK_EQ(qs[5].bid_venues, static_cast<std::uint8_t>(1));
    CHECK_EQ(qs[5].ask_px, 10002);
    CHECK_EQ(s.counters[1].gaps, 1);
    CHECK(s.final_state[1] == SessionState::Stale);
    CHECK(s.final_state[0] == SessionState::Live);
}

TEST(merge_tie_order_a_before_b_not_the_other_way) {
    // Same scenario: if B were processed first at 5 ms the first quote would be B-only (live 2).
    std::vector<MergedQuote> qs;
    run_merged(scenario_a(), scenario_b(), [&qs](const MergedQuote& q) { qs.push_back(q); });
    CHECK(!qs.empty());
    if (!qs.empty()) CHECK_EQ(qs[0].live_venues, static_cast<std::uint8_t>(1));
}

TEST(merge_latency_statistics_through_merge) {
    const MergeSummary s = run_merged(scenario_a(), scenario_b(), nullptr);
    // A: r - E*1000 = 1,000,000 and 1,500,000
    CHECK_EQ(s.latency[0].count, 2);
    CHECK_EQ(s.latency[0].sum_ns, 2500000);
    CHECK_EQ(s.latency[0].p50_ns, 1000000);  // nearest rank: s[(50*2+99)/100-1] = s[0]
    CHECK_EQ(s.latency[0].p99_ns, 1500000);  // s[(99*2+99)/100-1] = s[1]
    CHECK_EQ(s.latency[0].min_ns, 1000000);
    CHECK_EQ(s.latency[0].max_ns, 1500000);
    CHECK(std::fabs(s.latency[0].mean_ns - 1250000.0) < 1e-9);
    // B: four snapshot rows at +500,000, then +1,000,000, +300,000 and -200,000 (negative allowed)
    CHECK_EQ(s.latency[1].count, 7);
    CHECK_EQ(s.latency[1].sum_ns, 3100000);
    CHECK_EQ(s.latency[1].p50_ns, 500000);   // sorted s[3]
    CHECK_EQ(s.latency[1].p99_ns, 1000000);  // sorted s[6]
    CHECK_EQ(s.latency[1].min_ns, -200000);
    CHECK_EQ(s.latency[1].max_ns, 1000000);
    CHECK(std::fabs(s.latency[1].mean_ns - 3100000.0 / 7.0) < 1e-6);
}

TEST(merge_single_venue_with_empty_other_feed) {
    std::vector<MergedQuote> qs;
    const Feed empty_b = feed_of(Venue::B, {});
    const MergeSummary s = run_merged(scenario_a(), empty_b, [&qs](const MergedQuote& q) { qs.push_back(q); });
    CHECK_EQ(qs.size(), static_cast<std::size_t>(2));
    for (const MergedQuote& q : qs) CHECK_EQ(q.live_venues, static_cast<std::uint8_t>(1));
    CHECK_EQ(s.latency[1].count, 0);
    CHECK(s.final_state[1] == SessionState::Init);
    // and the other way round
    qs.clear();
    const Feed empty_a = feed_of(Venue::A, {});
    run_merged(empty_a, scenario_b(), [&qs](const MergedQuote& q) { qs.push_back(q); });
    for (const MergedQuote& q : qs) CHECK(q.live_venues == 0 || q.live_venues == 2);
}

TEST(merge_parse_errors_are_counted_and_skipped) {
    Feed a = scenario_a();
    a.lines.insert(a.lines.begin() + 1, "garbage");
    std::vector<MergedQuote> qs;
    const MergeSummary s = run_merged(a, scenario_b(), [&qs](const MergedQuote& q) { qs.push_back(q); });
    CHECK_EQ(s.counters[0].parse_errors, 1);
    CHECK_EQ(s.latency[0].count, 2);  // the bad line gives no latency sample
    CHECK_EQ(qs.size(), static_cast<std::size_t>(6));
}

TEST(merge_output_is_ordered_by_local_time) {
    std::int64_t prev = 0;
    std::int64_t n = 0;
    run_merged(load(Venue::A), load(Venue::B), [&](const MergedQuote& q) {
        CHECK(q.local_ts_ns >= prev);
        prev = q.local_ts_ns;
        ++n;
    });
    CHECK(n > 0);
}

TEST(merge_golden_matches_expected_merged_csv_byte_for_byte) {
    std::ostringstream os;
    write_merged_header(os);
    const MergeSummary s = run_merged(load(Venue::A), load(Venue::B),
                                      [&os](const MergedQuote& q) { write_merged_row(os, q); });
    const std::string want = slurp(golden("expected_merged.csv"));
    CHECK(!want.empty());
    CHECK(os.str() == want);
    if (os.str() != want) {
        std::istringstream g(os.str()), w(want);
        std::string gl, wl;
        for (int ln = 1; std::getline(g, gl) && std::getline(w, wl); ++ln)
            if (gl != wl) {
                std::cerr << "first merged diff at line " << ln << "\n  got  " << gl << "\n  want " << wl << "\n";
                break;
            }
    }
    CHECK_EQ(s.merged_quotes, 44);
    CHECK_EQ(s.merged_crossed, 0);
    CHECK_EQ(s.merged_locked, 2);
}

TEST(merge_golden_summary_matches_expected_counters_csv) {
    std::map<std::string, std::map<std::string, std::string>> e;
    std::vector<std::string> lines;
    CHECK(read_lines(golden("expected_counters.csv"), lines));
    for (std::size_t i = 1; i < lines.size(); ++i) {
        std::string_view f[4];
        CHECK_EQ(split_csv(lines[i], f, 4), static_cast<std::size_t>(3));
        e[std::string(f[0])][std::string(f[1])] = std::string(f[2]);
    }
    const MergeSummary s = run_merged(load(Venue::A), load(Venue::B), nullptr);
    CHECK_EQ(s.merged_quotes, std::stoll(e["merge"]["merged_quotes"]));
    CHECK_EQ(s.merged_crossed, std::stoll(e["merge"]["merged_crossed"]));
    CHECK_EQ(s.merged_locked, std::stoll(e["merge"]["merged_locked"]));
    for (int v = 0; v < 2; ++v) {
        const std::string name = v == 0 ? "A" : "B";
        auto& m = e[name];
        for (std::size_t i = 0; i < kNumCounters; ++i)
            CHECK_EQ(counter_value(s.counters[v], i), std::stoll(m.at(counter_name(i))));
        CHECK_EQ(std::string(to_string(s.final_state[v])), m.at("final_state"));
        CHECK_EQ(s.latency[v].count, std::stoll(m.at("latency_count")));
        CHECK_EQ(s.latency[v].sum_ns, std::stoll(m.at("latency_sum_ns")));
        CHECK_EQ(s.latency[v].p50_ns, std::stoll(m.at("latency_p50_ns")));
        CHECK_EQ(s.latency[v].p99_ns, std::stoll(m.at("latency_p99_ns")));
        CHECK_EQ(s.latency[v].min_ns, std::stoll(m.at("latency_min_ns")));
        CHECK_EQ(s.latency[v].max_ns, std::stoll(m.at("latency_max_ns")));
        CHECK(std::fabs(s.latency[v].mean_ns - std::stod(m.at("latency_mean_ns"))) < 0.05);
    }
}

TEST(merge_summary_json_shape) {
    const MergeSummary s = run_merged(scenario_a(), scenario_b(), nullptr);
    const std::string j = merge_summary_json(s);
    CHECK(j.rfind("{\"A\":{\"venue\":\"A\"", 0) == 0);
    CHECK(j.find(",\"B\":{\"venue\":\"B\"") != std::string::npos);
    CHECK(j.find(",\"merged_quotes\":6,\"merged_crossed\":1,\"merged_locked\":1}") != std::string::npos);
}

TEST(merge_is_deterministic) {
    std::ostringstream o1, o2;
    run_merged(load(Venue::A), load(Venue::B), [&o1](const MergedQuote& q) { write_merged_row(o1, q); });
    run_merged(load(Venue::A), load(Venue::B), [&o2](const MergedQuote& q) { write_merged_row(o2, q); });
    CHECK(o1.str() == o2.str());
}
