// Single-venue normalise / replay drivers used by mdtool and the tests (additive to DESIGN.md 11).
#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "md/io.hpp"
#include "md/session.hpp"
#include "md/stats.hpp"

namespace md {

// Checkpoint truth: rows keyed by the msg_index of the snapshot's first line.
struct TruthLevels {
    std::vector<Level> bids, asks;  // best first (file order of `level`)
};
using TruthMap = std::unordered_map<std::int64_t, TruthLevels>;
bool load_truth(const std::string& path, TruthMap& out, std::string& err);

// Writes the event CSV (with header) for every line. Parse failures are skipped and reported to
// `err` as "parse error line <msg_index>: <reason>". Returns the number of parse errors.
std::int64_t normalize_feed(const Feed& feed, std::ostream& out, std::ostream& err);

struct ReplayOptions {
    const TruthMap* truth = nullptr;  // enables the before-snapshot truth check
    std::ostream* tob = nullptr;      // top-of-book change log (header written first)
    std::ostream* err = nullptr;      // parse error reports
    SessionConfig session;
};
struct ReplayResult {
    VenueCounters counters;
    SessionState final_state = SessionState::Init;
    LatencySummary latency;
    std::int64_t truth_checked = 0, truth_mismatched = 0;
};
ReplayResult replay_feed(const Feed& feed, const ReplayOptions& opt);

// Counters JSON line; with_truth inserts ,"truth_checked":N,"truth_mismatched":M before final_state.
std::string replay_json(Venue v, const ReplayResult& r, bool with_truth);

}  // namespace md
