// Cross-venue merged best bid/offer (DESIGN.md section 8).
#pragma once

#include <cstdint>
#include <functional>

#include "md/io.hpp"
#include "md/session.hpp"
#include "md/stats.hpp"
#include "md/types.hpp"

namespace md {

class Merger {
public:
    // Record venue v's top after a message at local_ts_ns. Returns true if the merged quote
    // changed (then current() holds it with local_ts_ns set).
    bool update(Venue v, const TopOfBook& t, std::int64_t local_ts_ns);
    const MergedQuote& current() const { return last_; }

private:
    TopOfBook tops_[2];
    MergedQuote last_;
};

struct MergeSummary {
    VenueCounters counters[2];
    SessionState final_state[2] = {SessionState::Init, SessionState::Init};
    LatencySummary latency[2];
    std::int64_t merged_quotes = 0, merged_crossed = 0, merged_locked = 0;
};

// Parses both feeds, runs one VenueSession per venue in section 8 order, calls sink for every
// emitted merged quote. `a` is always Venue A and `b` Venue B. An empty Feed is allowed
// (single-venue run). Lines that fail to parse are skipped and counted (parse_errors).
MergeSummary run_merged(const Feed& a, const Feed& b,
                        const std::function<void(const MergedQuote&)>& sink);

// Extra helpers (additive to DESIGN.md section 11).
void write_merged_header(std::ostream& os);
void write_merged_row(std::ostream& os, const MergedQuote& q);
// {"A":<counters json>,"B":<counters json>,"merged_quotes":..,"merged_crossed":..,"merged_locked":..}
std::string merge_summary_json(const MergeSummary& s);

}  // namespace md
