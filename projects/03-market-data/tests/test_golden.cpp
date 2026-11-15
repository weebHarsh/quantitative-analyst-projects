// Golden-file tests: parse, normalise and replay the committed synthetic fixtures.
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "harness.hpp"
#include "md/parser_b.hpp"
#include "md/replay.hpp"

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

// expected_counters.csv: scope,key,value (values kept as text; means are compared numerically)
std::map<std::string, std::map<std::string, std::string>> load_expected_counters() {
    std::map<std::string, std::map<std::string, std::string>> m;
    std::vector<std::string> lines;
    CHECK(read_lines(golden("expected_counters.csv"), lines));
    CHECK(!lines.empty() && lines[0] == "scope,key,value");
    for (std::size_t i = 1; i < lines.size(); ++i) {
        std::string_view f[4];
        CHECK_EQ(split_csv(lines[i], f, 4), static_cast<std::size_t>(3));
        m[std::string(f[0])][std::string(f[1])] = std::string(f[2]);
    }
    return m;
}

Feed load(Venue v) {
    Feed f;
    std::string err;
    CHECK(load_feed(golden(v == Venue::A ? "venueA.jsonl" : "venueB.csv"), v, f, err));
    return f;
}

const char* vname(Venue v) { return v == Venue::A ? "A" : "B"; }

std::string normalized(const Feed& f, std::int64_t* errors = nullptr) {
    std::ostringstream out, err;
    const std::int64_t n = normalize_feed(f, out, err);
    if (errors != nullptr) *errors = n;
    CHECK(err.str().empty());
    return out.str();
}

}  // namespace

TEST(golden_load_feed_shapes) {
    CHECK_EQ(load(Venue::A).lines.size(), static_cast<std::size_t>(44));
    CHECK_EQ(load(Venue::B).lines.size(), static_cast<std::size_t>(87));  // header removed
    Feed f;
    std::string err;
    CHECK(!load_feed(golden("does_not_exist.csv"), Venue::B, f, err));
    CHECK(!err.empty());
    CHECK(!load_feed(golden("venueA.jsonl"), Venue::B, f, err));  // not a Venue B header
}

TEST(golden_normalize_matches_expected_events) {
    for (Venue v : {Venue::A, Venue::B}) {
        std::int64_t errors = -1;
        const std::string got = normalized(load(v), &errors);
        const std::string want = slurp(golden(std::string("expected_events_") + vname(v) + ".csv"));
        CHECK_EQ(errors, 0);
        CHECK(!want.empty());
        CHECK(got == want);
        if (got != want) {  // locate the first differing line for the report
            std::istringstream g(got), w(want);
            std::string gl, wl;
            for (int ln = 1; std::getline(g, gl) && std::getline(w, wl); ++ln) {
                if (gl != wl) {
                    std::cerr << "venue " << vname(v) << " line " << ln << ":\n  got  " << gl << "\n  want " << wl << "\n";
                    break;
                }
            }
        }
    }
}

TEST(golden_counters_match_expected) {
    const auto exp = load_expected_counters();
    for (Venue v : {Venue::A, Venue::B}) {
        const auto& e = exp.at(vname(v));
        const ReplayResult r = replay_feed(load(v), ReplayOptions{});
        for (std::size_t i = 0; i < kNumCounters; ++i) {
            const std::string name = counter_name(i);
            const auto it = e.find(name);
            CHECK(it != e.end());
            if (it == e.end()) continue;
            if (counter_value(r.counters, i) != std::stoll(it->second))
                std::cerr << vname(v) << " counter " << name << ": got " << counter_value(r.counters, i)
                          << " want " << it->second << "\n";
            CHECK_EQ(counter_value(r.counters, i), std::stoll(it->second));
        }
        CHECK_EQ(std::string(to_string(r.final_state)), e.at("final_state"));
        CHECK_EQ(r.latency.count, std::stoll(e.at("latency_count")));
        CHECK_EQ(r.latency.sum_ns, std::stoll(e.at("latency_sum_ns")));
        CHECK_EQ(r.latency.p50_ns, std::stoll(e.at("latency_p50_ns")));
        CHECK_EQ(r.latency.p99_ns, std::stoll(e.at("latency_p99_ns")));
        CHECK_EQ(r.latency.min_ns, std::stoll(e.at("latency_min_ns")));
        CHECK_EQ(r.latency.max_ns, std::stoll(e.at("latency_max_ns")));
        CHECK(std::fabs(r.latency.mean_ns - std::stod(e.at("latency_mean_ns"))) < 0.05);
        // recompute the mean from sum / count as well
        CHECK(std::fabs(static_cast<double>(r.latency.sum_ns) / static_cast<double>(r.latency.count) -
                        std::stod(e.at("latency_mean_ns"))) < 0.05);
    }
}

TEST(golden_expected_counter_values_from_design) {
    // Pins the values quoted in DESIGN.md section 4 so a silent fixture change is noticed.
    const ReplayResult a = replay_feed(load(Venue::A), ReplayOptions{});
    const ReplayResult b = replay_feed(load(Venue::B), ReplayOptions{});
    for (const ReplayResult* r : {&a, &b}) {
        CHECK_EQ(r->counters.gaps, 1);
        CHECK_EQ(r->counters.resyncs, 2);
        CHECK_EQ(r->counters.duplicates_ignored, 1);
        CHECK_EQ(r->counters.out_of_order, 0);
        CHECK_EQ(r->counters.crossed_detected, 1);
        CHECK_EQ(r->counters.checkpoints_skipped_stale, 2);
        CHECK(r->final_state == SessionState::Live);
    }
    CHECK_EQ(a.counters.checkpoints_matched, 2);
    CHECK_EQ(b.counters.checkpoints_matched, 3);
}

TEST(golden_checkpoints_equal_truth) {
    for (Venue v : {Venue::A, Venue::B}) {
        TruthMap truth;
        std::string err;
        CHECK(load_truth(golden(std::string("truth_") + vname(v) + ".csv"), truth, err));
        CHECK(!truth.empty());
        ReplayOptions opt;
        opt.truth = &truth;
        const ReplayResult r = replay_feed(load(v), opt);
        CHECK_EQ(r.truth_mismatched, 0);
        CHECK(r.truth_checked > 0);
        CHECK_EQ(r.truth_checked, r.counters.checkpoints_matched);
        const std::string js = replay_json(v, r, true);
        CHECK(js.find(",\"truth_checked\":" + std::to_string(r.truth_checked) +
                      ",\"truth_mismatched\":0,\"final_state\":\"LIVE\"") != std::string::npos);
    }
}

TEST(golden_truth_detects_a_corrupted_checkpoint) {
    TruthMap truth;
    std::string err;
    CHECK(load_truth(golden("truth_A.csv"), truth, err));
    for (auto& kv : truth) {
        if (!kv.second.bids.empty()) kv.second.bids[0].qty_lots += 1;
    }
    ReplayOptions opt;
    opt.truth = &truth;
    const ReplayResult r = replay_feed(load(Venue::A), opt);
    CHECK_EQ(r.truth_mismatched, r.truth_checked);
    CHECK(r.truth_mismatched > 0);
}

TEST(golden_tob_log) {
    std::ostringstream tob;
    ReplayOptions opt;
    opt.tob = &tob;
    replay_feed(load(Venue::A), opt);
    std::istringstream in(tob.str());
    std::string line;
    std::getline(in, line);
    CHECK_EQ(line, std::string("local_ts_ns,live,bid_ticks,bid_lots,ask_ticks,ask_lots"));
    std::getline(in, line);  // first message is the initial snapshot: top becomes live
    CHECK_EQ(line, std::string("1700000000000487848,1,10000,5000,10001,2100"));
    int rows = 0, not_live = 0;
    std::int64_t prev_ts = 0;
    while (std::getline(in, line)) {
        ++rows;
        std::string_view f[7];
        CHECK_EQ(split_csv(line, f, 7), static_cast<std::size_t>(6));
        if (f[1] == "0") ++not_live;
        const std::int64_t ts = std::stoll(std::string(f[0]));
        CHECK(ts > prev_ts);
        prev_ts = ts;
    }
    CHECK(rows > 0);
    CHECK(not_live >= 2);  // stale periods (gap, crossed) publish a not-live top
}

TEST(golden_replay_is_deterministic) {
    const ReplayResult r1 = replay_feed(load(Venue::B), ReplayOptions{});
    const ReplayResult r2 = replay_feed(load(Venue::B), ReplayOptions{});
    CHECK_EQ(replay_json(Venue::B, r1, false), replay_json(Venue::B, r2, false));
}

TEST(golden_parse_errors_are_counted_not_fatal) {
    for (Venue v : {Venue::A, Venue::B}) {
        Feed f = load(v);
        const std::size_t n = f.lines.size();
        f.lines.insert(f.lines.begin() + 5, "this is not a valid line");
        f.lines.insert(f.lines.begin() + 9, "");
        std::ostringstream err;
        ReplayOptions opt;
        opt.err = &err;
        const ReplayResult r = replay_feed(f, opt);
        CHECK_EQ(r.counters.parse_errors, 2);
        CHECK_EQ(r.counters.messages, static_cast<std::int64_t>(n) + 2);
        CHECK(err.str().find("parse error line 5: ") != std::string::npos);
        CHECK(err.str().find("parse error line 9: ") != std::string::npos);
        // normalize skips them and reports on stderr
        std::ostringstream out, nerr;
        CHECK_EQ(normalize_feed(f, out, nerr), 2);
        CHECK(nerr.str().find("parse error line 5: ") != std::string::npos);
    }
}
