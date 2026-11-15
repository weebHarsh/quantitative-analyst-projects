#include "md/merge.hpp"

#include <sstream>
#include <string>
#include <vector>

#include <algorithm>

#include "md/parser_a.hpp"
#include "md/parser_b.hpp"
#include "md/saturate.hpp"

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
                q.bid_qty = sat_add(q.bid_qty, tb.bid_qty);
                q.bid_venues = static_cast<std::uint8_t>(q.bid_venues | bit);
            }
        }
        if (tb.ask_px != 0 && tb.ask_qty != 0) {
            if (q.ask_venues == 0 || tb.ask_px < q.ask_px) {
                q.ask_px = tb.ask_px;
                q.ask_qty = tb.ask_qty;
                q.ask_venues = bit;
            } else if (tb.ask_px == q.ask_px) {
                q.ask_qty = sat_add(q.ask_qty, tb.ask_qty);
                q.ask_venues = static_cast<std::uint8_t>(q.ask_venues | bit);
            }
        }
    }
    if (same_quote(q, last_)) return false;
    last_ = q;
    return true;
}

namespace {

struct Msg {
    std::int64_t ts;        // local receive time of the message
    std::size_t begin, end; // its events in the shared event vector
    Venue venue;
};

// Parse every line of one feed. Lines that fail are counted on the venue's session and skipped.
void load_feed(const Feed& f, Venue v, std::vector<Event>& evs, std::vector<Msg>& msgs, VenueSession& session) {
    ParserA pa;
    ParserB pb;
    std::vector<Event> tmp;
    for (std::size_t i = 0; i < f.lines.size(); ++i) {
        tmp.clear();
        const std::int64_t idx = static_cast<std::int64_t>(i);
        const ParseStatus st = v == Venue::A ? pa.parse(f.lines[i], idx, tmp) : pb.parse(f.lines[i], idx, tmp);
        if (st == ParseStatus::Error) {
            session.note_parse_error();
            continue;
        }
        if (tmp.empty()) continue;
        Msg m;
        m.ts = tmp.front().local_ts_ns;
        m.begin = evs.size();
        evs.insert(evs.end(), tmp.begin(), tmp.end());
        m.end = evs.size();
        m.venue = v;
        msgs.push_back(m);
    }
}

}  // namespace

MergeSummary run_merged(const Feed& a, const Feed& b,
                        const std::function<void(const MergedQuote&)>& sink) {
    VenueSession sess[2] = {VenueSession(Venue::A), VenueSession(Venue::B)};
    LatencyStats lat[2];
    std::vector<Event> evs;
    std::vector<Msg> msgs;
    evs.reserve(2 * (a.lines.size() + b.lines.size()) + 16);
    msgs.reserve(a.lines.size() + b.lines.size());
    load_feed(a, Venue::A, evs, msgs, sess[0]);
    load_feed(b, Venue::B, evs, msgs, sess[1]);
    // Global order is (local_ts_ns, venue id, file order). A's block precedes B's in `msgs` and
    // the sort is stable, so sorting on (ts, venue) gives exactly that, even when a feed's own
    // timestamps are out of order. This matches the reference pipeline in tools/gen_feeds.py.
    std::stable_sort(msgs.begin(), msgs.end(), [](const Msg& x, const Msg& y) {
        return x.ts != y.ts ? x.ts < y.ts : x.venue < y.venue;
    });

    Merger merger;
    MergeSummary s;
    for (const Msg& m : msgs) {
        const std::size_t vi = static_cast<std::size_t>(m.venue);
        lat[vi].add(sat_sub(evs[m.begin].local_ts_ns, evs[m.begin].exch_ts_ns));
        for (std::size_t i = m.begin; i < m.end; ++i) sess[vi].on_event(evs[i]);
        if (merger.update(m.venue, sess[vi].top(), m.ts)) {
            const MergedQuote& q = merger.current();
            ++s.merged_quotes;
            if (q.bid_venues != 0 && q.ask_venues != 0) {
                if (q.bid_px > q.ask_px) ++s.merged_crossed;
                else if (q.bid_px == q.ask_px) ++s.merged_locked;
            }
            if (sink) sink(q);
        }
    }
    for (std::size_t i = 0; i < 2; ++i) {
        s.counters[i] = sess[i].counters();
        s.final_state[i] = sess[i].state();
        s.latency[i] = lat[i].summary();
    }
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
