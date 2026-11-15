#include "bt/event_csv.hpp"

#include <fstream>
#include <limits>

#include "md/fixed.hpp"

namespace bt {

namespace {

constexpr const char* kHeader =
    "venue,type,side,price_ticks,qty_lots,exch_ts_ns,local_ts_ns,seq_first,seq_last,prev_seq,"
    "msg_index,flags";

bool parse_type(std::string_view s, md::EventType& out) {
    for (int i = 0; i <= static_cast<int>(md::EventType::Trade); ++i) {
        const auto t = static_cast<md::EventType>(i);
        if (s == md::to_string(t)) {
            out = t;
            return true;
        }
    }
    return false;
}

bool parse_side(std::string_view s, md::Side& out) {
    for (md::Side c : {md::Side::None, md::Side::Bid, md::Side::Ask}) {
        if (s == md::to_string(c)) {
            out = c;
            return true;
        }
    }
    return false;
}

bool parse_seq(std::string_view s, std::int64_t& out) {
    if (s == "-1") {
        out = md::kNone;
        return true;
    }
    return md::parse_uint(s, out);
}

}  // namespace

bool parse_event_csv_line(std::string_view line, md::Event& out, std::string& err) {
    std::string_view f[12];
    std::size_t n = 0, start = 0;
    for (std::size_t i = 0; i <= line.size(); ++i) {
        if (i == line.size() || line[i] == ',') {
            if (n < 12) f[n] = line.substr(start, i - start);
            ++n;
            start = i + 1;
        }
    }
    if (n != 12) {
        err = "expected 12 fields, got " + std::to_string(n);
        return false;
    }
    md::Event e;
    std::int64_t flags = 0;
    if (!md::parse_venue(f[0], e.venue)) { err = "unknown venue '" + std::string(f[0]) + "'"; return false; }
    if (!parse_type(f[1], e.type)) { err = "unknown type '" + std::string(f[1]) + "'"; return false; }
    if (!parse_side(f[2], e.side)) { err = "unknown side '" + std::string(f[2]) + "'"; return false; }
    if (!md::parse_uint(f[3], e.price_ticks)) { err = "bad price_ticks '" + std::string(f[3]) + "'"; return false; }
    if (!md::parse_uint(f[4], e.qty_lots)) { err = "bad qty_lots '" + std::string(f[4]) + "'"; return false; }
    if (!md::parse_uint(f[5], e.exch_ts_ns)) { err = "bad exch_ts_ns '" + std::string(f[5]) + "'"; return false; }
    if (!md::parse_uint(f[6], e.local_ts_ns)) { err = "bad local_ts_ns '" + std::string(f[6]) + "'"; return false; }
    if (!parse_seq(f[7], e.seq_first)) { err = "bad seq_first '" + std::string(f[7]) + "'"; return false; }
    if (!parse_seq(f[8], e.seq_last)) { err = "bad seq_last '" + std::string(f[8]) + "'"; return false; }
    if (!parse_seq(f[9], e.prev_seq)) { err = "bad prev_seq '" + std::string(f[9]) + "'"; return false; }
    if (!md::parse_uint(f[10], e.msg_index)) { err = "bad msg_index '" + std::string(f[10]) + "'"; return false; }
    if (!md::parse_uint(f[11], flags) || flags > 255) { err = "bad flags '" + std::string(f[11]) + "'"; return false; }
    e.flags = static_cast<std::uint8_t>(flags);
    out = e;
    return true;
}

bool group_messages(const std::vector<md::Event>& ev, std::vector<Message>& out, std::string& err) {
    out.clear();
    std::size_t i = 0;
    std::int64_t prev_ts = std::numeric_limits<std::int64_t>::min();
    while (i < ev.size()) {
        const md::Event& first = ev[i];
        Message m;
        m.begin = i;
        m.ts_ns = first.local_ts_ns;
        m.is_trade = first.type == md::EventType::Trade;
        std::size_t j = i;
        for (;;) {
            const md::Event& e = ev[j];
            if (e.venue != ev[0].venue) {
                err = "event " + std::to_string(j) + ": more than one venue in the file";
                return false;
            }
            if (e.msg_index != first.msg_index || e.local_ts_ns != first.local_ts_ns) {
                err = "event " + std::to_string(j) + ": msg_index or local_ts_ns changes inside a message";
                return false;
            }
            if ((e.type == md::EventType::Trade) != m.is_trade) {
                err = "event " + std::to_string(j) + ": message mixes TRADE with other event types";
                return false;
            }
            ++j;
            if ((e.flags & md::kLastInMsg) != 0) break;
            if (j == ev.size()) {
                err = "event " + std::to_string(j - 1) + ": last message is not terminated (no flags&1)";
                return false;
            }
        }
        m.end = j;
        if (m.ts_ns < prev_ts) {
            err = "event " + std::to_string(i) + ": local_ts_ns goes backwards";
            return false;
        }
        prev_ts = m.ts_ns;
        out.push_back(m);
        i = j;
    }
    return true;
}

bool read_event_csv(const std::string& path, std::vector<md::Event>& out, std::string& err) {
    out.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "cannot open '" + path + "'";
        return false;
    }
    std::string line;
    if (!std::getline(in, line) || line != kHeader) {
        err = "'" + path + "': bad or missing header";
        return false;
    }
    std::size_t lineno = 1;
    while (std::getline(in, line)) {
        ++lineno;
        md::Event e;
        std::string why;
        if (!parse_event_csv_line(line, e, why)) {
            err = "line " + std::to_string(lineno) + ": " + why;
            return false;
        }
        out.push_back(e);
    }
    std::vector<Message> msgs;
    if (!group_messages(out, msgs, err)) {
        err = "'" + path + "': " + err;
        return false;
    }
    return true;
}

}  // namespace bt
