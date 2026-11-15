#include "md/io.hpp"

#include <charconv>
#include <cstdio>
#include <fstream>

#include "md/parser_b.hpp"

namespace md {

bool read_lines(const std::string& path, std::vector<std::string>& lines) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    lines.clear();
    std::string s;
    while (std::getline(in, s)) {
        if (!s.empty() && s.back() == '\r') s.pop_back();
        lines.push_back(std::move(s));
        s.clear();
    }
    return true;
}

bool load_feed(const std::string& path, Venue v, Feed& out, std::string& err) {
    out.venue = v;
    if (!read_lines(path, out.lines)) {
        err = "cannot open '" + path + "'";
        return false;
    }
    if (v == Venue::B) {
        if (out.lines.empty() || out.lines.front() != kVenueBHeader) {
            err = "'" + path + "': bad or missing Venue B header";
            return false;
        }
        out.lines.erase(out.lines.begin());
    }
    return true;
}

void write_event_csv_header(std::ostream& os) {
    os << "venue,type,side,price_ticks,qty_lots,exch_ts_ns,local_ts_ns,seq_first,seq_last,prev_seq,"
          "msg_index,flags\n";
}

namespace {
char* put_int(char* p, char* end, std::int64_t v) {
    return std::to_chars(p, end, v).ptr;
}
}  // namespace

void write_event_csv(std::ostream& os, const Event& e) {
    char buf[320];
    char* p = buf;
    char* const end = buf + sizeof(buf);
    auto put_str = [&p](const char* s) {
        while (*s != '\0') *p++ = *s++;
    };
    put_str(to_string(e.venue));
    *p++ = ',';
    put_str(to_string(e.type));
    *p++ = ',';
    put_str(to_string(e.side));
    for (std::int64_t v : {e.price_ticks, e.qty_lots, e.exch_ts_ns, e.local_ts_ns, e.seq_first,
                           e.seq_last, e.prev_seq, e.msg_index, static_cast<std::int64_t>(e.flags)}) {
        *p++ = ',';
        p = put_int(p, end, v);
    }
    *p++ = '\n';
    os.write(buf, p - buf);
}

void write_counters_json(std::ostream& os, Venue v, const VenueCounters& c, SessionState s,
                         const LatencySummary& lat) {
    os << "{\"venue\":\"" << to_string(v) << '"';
    for (std::size_t i = 0; i < kNumCounters; ++i)
        os << ",\"" << counter_name(i) << "\":" << counter_value(c, i);
    char mean[64];
    std::snprintf(mean, sizeof(mean), "%.1f", lat.mean_ns);
    os << ",\"final_state\":\"" << to_string(s) << "\",\"latency\":{\"count\":" << lat.count
       << ",\"sum_ns\":" << lat.sum_ns << ",\"mean_ns\":" << mean << ",\"p50_ns\":" << lat.p50_ns
       << ",\"p99_ns\":" << lat.p99_ns << ",\"min_ns\":" << lat.min_ns << ",\"max_ns\":" << lat.max_ns
       << "}}";
}

}  // namespace md
