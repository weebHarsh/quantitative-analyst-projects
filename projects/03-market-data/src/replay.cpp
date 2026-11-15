#include "md/replay.hpp"

#include <fstream>
#include <sstream>

#include "md/fixed.hpp"
#include "md/parser_a.hpp"
#include "md/parser_b.hpp"

namespace md {
namespace {

// Dispatches to the right parser for a venue.
class LineParser {
public:
    explicit LineParser(Venue v) : venue_(v) {}
    ParseStatus parse(std::string_view line, std::int64_t idx, std::vector<Event>& out) {
        return venue_ == Venue::A ? a_.parse(line, idx, out) : b_.parse(line, idx, out);
    }
    const std::string& error() const { return venue_ == Venue::A ? a_.last_error() : b_.last_error(); }

private:
    Venue venue_;
    ParserA a_;
    ParserB b_;
};

}  // namespace

bool load_truth(const std::string& path, TruthMap& out, std::string& err) {
    std::vector<std::string> lines;
    if (!read_lines(path, lines)) {
        err = "cannot open '" + path + "'";
        return false;
    }
    out.clear();
    if (lines.empty() || lines.front() != "checkpoint,seq,msg_index,local_ts_ns,side,level,price_ticks,qty_lots") {
        err = "'" + path + "': bad truth header";
        return false;
    }
    for (std::size_t i = 1; i < lines.size(); ++i) {
        std::string_view f[9];
        if (split_csv(lines[i], f, 9) != 8) {
            err = "'" + path + "': bad truth row " + std::to_string(i);
            return false;
        }
        std::int64_t idx = 0, level = 0, px = 0, qty = 0;
        if (!parse_uint(f[2], idx) || !parse_uint(f[5], level) || !parse_uint(f[6], px) ||
            !parse_uint(f[7], qty) || (f[4] != "BID" && f[4] != "ASK")) {
            err = "'" + path + "': bad truth row " + std::to_string(i);
            return false;
        }
        auto& t = out[idx];
        auto& v = f[4] == "BID" ? t.bids : t.asks;
        if (static_cast<std::int64_t>(v.size()) != level) {
            err = "'" + path + "': truth levels out of order at row " + std::to_string(i);
            return false;
        }
        v.push_back(Level{px, qty});
    }
    return true;
}

std::int64_t normalize_feed(const Feed& feed, std::ostream& out, std::ostream& err) {
    LineParser parser(feed.venue);
    std::vector<Event> evs;
    std::int64_t errors = 0;
    write_event_csv_header(out);
    for (std::size_t i = 0; i < feed.lines.size(); ++i) {
        evs.clear();
        if (parser.parse(feed.lines[i], static_cast<std::int64_t>(i), evs) == ParseStatus::Error) {
            ++errors;
            err << "parse error line " << i << ": " << parser.error() << '\n';
            continue;
        }
        for (const Event& e : evs) write_event_csv(out, e);
    }
    return errors;
}

ReplayResult replay_feed(const Feed& feed, const ReplayOptions& opt) {
    ReplayResult res;
    VenueSession session(feed.venue, opt.session);
    if (opt.truth != nullptr) {
        const TruthMap* truth = opt.truth;
        ReplayResult* rp = &res;
        session.set_before_snapshot_hook([truth, rp](const VenueSession& s, std::int64_t snap_seq,
                                                     std::int64_t begin_idx) {
            if (s.state() != SessionState::Live || s.last_applied() != snap_seq) return;
            ++rp->truth_checked;
            auto it = truth->find(begin_idx);
            std::vector<Level> bids, asks;
            s.book().top_n(Side::Bid, 10, bids);
            s.book().top_n(Side::Ask, 10, asks);
            if (it == truth->end() || it->second.bids != bids || it->second.asks != asks)
                ++rp->truth_mismatched;
        });
    }
    if (opt.tob != nullptr) *opt.tob << "local_ts_ns,live,bid_ticks,bid_lots,ask_ticks,ask_lots\n";

    LineParser parser(feed.venue);
    LatencyStats lat;
    lat.reserve(feed.lines.size());
    std::vector<Event> evs;
    TopOfBook last_top;
    for (std::size_t i = 0; i < feed.lines.size(); ++i) {
        evs.clear();
        if (parser.parse(feed.lines[i], static_cast<std::int64_t>(i), evs) == ParseStatus::Error) {
            session.note_parse_error();
            if (opt.err != nullptr) *opt.err << "parse error line " << i << ": " << parser.error() << '\n';
            continue;
        }
        lat.add(evs.front().local_ts_ns - evs.front().exch_ts_ns);
        for (const Event& e : evs) session.on_event(e);
        if (opt.tob != nullptr) {
            const TopOfBook t = session.top();
            if (t != last_top) {
                *opt.tob << evs.front().local_ts_ns << ',' << (t.live ? 1 : 0) << ',' << t.bid_px << ','
                         << t.bid_qty << ',' << t.ask_px << ',' << t.ask_qty << '\n';
                last_top = t;
            }
        }
    }
    res.counters = session.counters();
    res.final_state = session.state();
    res.latency = lat.summary();
    return res;
}

std::string replay_json(Venue v, const ReplayResult& r, bool with_truth) {
    std::ostringstream os;
    write_counters_json(os, v, r.counters, r.final_state, r.latency);
    std::string s = os.str();
    if (with_truth) {
        const std::string marker = ",\"final_state\"";
        const std::size_t pos = s.find(marker);
        if (pos != std::string::npos) {
            s.insert(pos, ",\"truth_checked\":" + std::to_string(r.truth_checked) +
                              ",\"truth_mismatched\":" + std::to_string(r.truth_mismatched));
        }
    }
    return s;
}

}  // namespace md
