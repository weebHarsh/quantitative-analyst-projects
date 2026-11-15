#include "md/merge.hpp"

#include <sstream>
#include <string>
#include <vector>

#include "md/parser_a.hpp"
#include "md/parser_b.hpp"

namespace md {

bool Merger::update(Venue v, const TopOfBook& t, std::int64_t local_ts_ns) {
    tops_[static_cast<std::size_t>(v)] = t;
    MergedQuote q;
    q.local_ts_ns = local_ts_ns;
    for (std::size_t i = 0; i < 2; ++i) {
        const TopOfBook& tb = tops_[i];
        if (!tb.live) continue;
        const std::uint8_t bit = static_cast<std::uint8_t>(1u << i);
        q.live_venues = static_cast<std::uint8_t>(q.live_venues | bit);
        if (tb.bid_px != 0 && tb.bid_qty != 0) {
            if (q.bid_venues == 0 || tb.bid_px > q.bid_px) {
                q.bid_px = tb.bid_px;
                q.bid_qty = tb.bid_qty;
                q.bid_venues = bit;
            } else if (tb.bid_px == q.bid_px) {
                q.bid_qty += tb.bid_qty;
                q.bid_venues = static_cast<std::uint8_t>(q.bid_venues | bit);
            }
        }
        if (tb.ask_px != 0 && tb.ask_qty != 0) {
            if (q.ask_venues == 0 || tb.ask_px < q.ask_px) {
                q.ask_px = tb.ask_px;
                q.ask_qty = tb.ask_qty;
                q.ask_venues = bit;
            } else if (tb.ask_px == q.ask_px) {
                q.ask_qty += tb.ask_qty;
                q.ask_venues = static_cast<std::uint8_t>(q.ask_venues | bit);
            }
        }
    }
    if (same_quote(q, last_)) return false;
    last_ = q;
    return true;
}

namespace {

// One venue's stream: lazily parses lines, skipping (and counting) the ones that fail.
class Cursor {
public:
    Cursor(const Feed& f, Venue v) : feed_(f), venue_(v), session_(v) {
        lat_.reserve(f.lines.size());
        advance();
    }
    bool have() const { return have_; }
    std::int64_t ts() const { return evs_.front().local_ts_ns; }

    // Process the current message, then move to the next parseable one.
    void consume() {
        lat_.add(evs_.front().local_ts_ns - evs_.front().exch_ts_ns);
        for (const Event& e : evs_) session_.on_event(e);
        advance();
    }
    // Top after the message just consumed (valid between consume() and the next consume()).
    TopOfBook top() const { return session_.top(); }
    const VenueSession& session() const { return session_; }
    const LatencyStats& lat() const { return lat_; }

private:
    void advance() {
        have_ = false;
        while (next_ < feed_.lines.size()) {
            evs_.clear();
            const std::int64_t idx = static_cast<std::int64_t>(next_++);
            const std::string_view line = feed_.lines[static_cast<std::size_t>(idx)];
            const ParseStatus st = venue_ == Venue::A ? pa_.parse(line, idx, evs_) : pb_.parse(line, idx, evs_);
            if (st == ParseStatus::Error) {
                session_.note_parse_error();
                continue;
            }
            have_ = true;
            return;
        }
    }

    const Feed& feed_;
    Venue venue_;
    ParserA pa_;
    ParserB pb_;
    VenueSession session_;
    LatencyStats lat_;
    std::vector<Event> evs_;
    std::size_t next_ = 0;
    bool have_ = false;
};

}  // namespace

MergeSummary run_merged(const Feed& a, const Feed& b,
                        const std::function<void(const MergedQuote&)>& sink) {
    Cursor ca(a, Venue::A), cb(b, Venue::B);
    Merger merger;
    MergeSummary s;
    while (ca.have() || cb.have()) {
        // Order by (local_ts_ns, venue id, msg_index); within a venue file order is msg_index.
        const bool take_a = ca.have() && (!cb.have() || ca.ts() <= cb.ts());
        Cursor& c = take_a ? ca : cb;
        const Venue v = take_a ? Venue::A : Venue::B;
        const std::int64_t ts = c.ts();
        c.consume();
        if (merger.update(v, c.top(), ts)) {
            const MergedQuote& q = merger.current();
            ++s.merged_quotes;
            if (q.bid_venues != 0 && q.ask_venues != 0) {
                if (q.bid_px > q.ask_px) ++s.merged_crossed;
                else if (q.bid_px == q.ask_px) ++s.merged_locked;
            }
            if (sink) sink(q);
        }
    }
    s.counters[0] = ca.session().counters();
    s.counters[1] = cb.session().counters();
    s.final_state[0] = ca.session().state();
    s.final_state[1] = cb.session().state();
    s.latency[0] = ca.lat().summary();
    s.latency[1] = cb.lat().summary();
    return s;
}

void write_merged_header(std::ostream& os) {
    os << "local_ts_ns,bid_ticks,bid_lots,bid_venues,ask_ticks,ask_lots,ask_venues,live_venues\n";
}

void write_merged_row(std::ostream& os, const MergedQuote& q) {
    os << q.local_ts_ns << ',' << q.bid_px << ',' << q.bid_qty << ',' << static_cast<unsigned>(q.bid_venues)
       << ',' << q.ask_px << ',' << q.ask_qty << ',' << static_cast<unsigned>(q.ask_venues) << ','
       << static_cast<unsigned>(q.live_venues) << '\n';
}

std::string merge_summary_json(const MergeSummary& s) {
    std::ostringstream os;
    os << "{\"A\":";
    write_counters_json(os, Venue::A, s.counters[0], s.final_state[0], s.latency[0]);
    os << ",\"B\":";
    write_counters_json(os, Venue::B, s.counters[1], s.final_state[1], s.latency[1]);
    os << ",\"merged_quotes\":" << s.merged_quotes << ",\"merged_crossed\":" << s.merged_crossed
       << ",\"merged_locked\":" << s.merged_locked << '}';
    return os.str();
}

}  // namespace md
