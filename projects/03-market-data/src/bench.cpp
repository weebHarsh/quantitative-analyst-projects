// Benchmark (DESIGN.md section 10). Feeds are read into memory by the caller and are not timed.
#include "md/bench.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <sstream>
#include <vector>

#include "md/merge.hpp"
#include "md/parser_a.hpp"
#include "md/parser_b.hpp"
#include "md/session.hpp"
#include "md_build_info.hpp"

namespace md {
namespace {

using Clock = std::chrono::steady_clock;

double seconds_since(Clock::time_point t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return n % 2 == 1 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

struct MsgRef {
    std::int64_t ts = 0;
    std::uint32_t begin = 0, end = 0;  // event index range [begin, end)
    std::uint8_t venue = 0;
};

bool is_update(EventType t) {
    return t == EventType::Add || t == EventType::Modify || t == EventType::Set || t == EventType::Delete;
}

// Parses every line of both feeds into `out` (A first, then B). Returns the parse error count.
std::int64_t parse_all(const Feed& a, const Feed& b, std::vector<Event>& out) {
    std::int64_t errors = 0;
    ParserA pa;
    ParserB pb;
    for (std::size_t i = 0; i < a.lines.size(); ++i)
        if (pa.parse(a.lines[i], static_cast<std::int64_t>(i), out) == ParseStatus::Error) ++errors;
    for (std::size_t i = 0; i < b.lines.size(); ++i)
        if (pb.parse(b.lines[i], static_cast<std::int64_t>(i), out) == ParseStatus::Error) ++errors;
    return errors;
}

// Message table in file order per venue (A block first), then the merged order of section 8.
void build_messages(const std::vector<Event>& evs, std::vector<MsgRef>& merged,
                    std::vector<MsgRef> per_venue[2]) {
    merged.clear();
    per_venue[0].clear();
    per_venue[1].clear();
    std::size_t i = 0;
    while (i < evs.size()) {
        MsgRef m;
        m.begin = static_cast<std::uint32_t>(i);
        m.ts = evs[i].local_ts_ns;
        m.venue = static_cast<std::uint8_t>(evs[i].venue);
        while (i < evs.size() && (evs[i].flags & kLastInMsg) == 0) ++i;
        if (i < evs.size()) ++i;
        m.end = static_cast<std::uint32_t>(i);
        per_venue[m.venue].push_back(m);
    }
    merged.reserve(per_venue[0].size() + per_venue[1].size());
    merged.insert(merged.end(), per_venue[0].begin(), per_venue[0].end());
    merged.insert(merged.end(), per_venue[1].begin(), per_venue[1].end());
    // (local_ts, venue id, file order): A's block precedes B's, so a stable sort on (ts, venue).
    std::stable_sort(merged.begin(), merged.end(), [](const MsgRef& x, const MsgRef& y) {
        return x.ts != y.ts ? x.ts < y.ts : x.venue < y.venue;
    });
}

std::int64_t percentile_permille(const std::vector<std::int64_t>& sorted, std::size_t permille) {
    if (sorted.empty()) return 0;
    const std::size_t n = sorted.size();
    const std::size_t rank = (permille * n + 999) / 1000;
    return sorted[std::min(rank > 0 ? rank - 1 : 0, n - 1)];
}

std::string json_escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    return o;
}

std::string fmt(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.1f", v);
    return buf;
}

}  // namespace

BenchResult run_bench(const Feed& a, const Feed& b, const BenchConfig& cfg) {
    BenchResult r;
    r.reps = cfg.reps;
    r.warmup = cfg.warmup;
    r.build = MD_BUILD_STRING;
    const int total_runs = cfg.warmup + cfg.reps;

    // ---- parse phase ------------------------------------------------------------------------
    std::vector<Event> evs;
    evs.reserve(2 * (a.lines.size() + b.lines.size()) + 16);
    std::vector<double> parse_s;
    for (int k = 0; k < total_runs; ++k) {
        evs.clear();
        const auto t0 = Clock::now();
        r.parse_errors = parse_all(a, b, evs);
        const double s = seconds_since(t0);
        if (k >= cfg.warmup) parse_s.push_back(s);
    }
    r.events = static_cast<std::int64_t>(evs.size());
    r.parse_events_per_s = static_cast<double>(r.events) / median(parse_s);

    std::vector<MsgRef> order, by_venue[2];
    build_messages(evs, order, by_venue);
    r.messages = static_cast<std::int64_t>(order.size());
    for (int v = 0; v < 2; ++v) {
        r.messages_by_venue[v] = static_cast<std::int64_t>(by_venue[v].size());
        for (const MsgRef& m : by_venue[v]) r.events_by_venue[v] += static_cast<std::int64_t>(m.end - m.begin);
    }

    // ---- apply phase (merged order, no merge) -----------------------------------------------
    VenueCounters ref[2];
    bool have_ref = false;
    auto check = [&](const VenueCounters& ca, const VenueCounters& cb) {
        if (!have_ref) {
            ref[0] = ca;
            ref[1] = cb;
            have_ref = true;
        } else if (ref[0] != ca || ref[1] != cb) {
            r.counters_stable = false;
        }
    };
    std::vector<double> apply_s;
    for (int k = 0; k < total_runs; ++k) {
        VenueSession sa(Venue::A), sb(Venue::B);
        VenueSession* const ss[2] = {&sa, &sb};
        const auto t0 = Clock::now();
        for (const MsgRef& m : order) {
            VenueSession& s = *ss[m.venue];
            for (std::uint32_t i = m.begin; i < m.end; ++i) s.on_event(evs[i]);
        }
        const double sec = seconds_since(t0);
        check(sa.counters(), sb.counters());
        if (k >= cfg.warmup) apply_s.push_back(sec);
    }
    r.apply_events_per_s = static_cast<double>(r.events) / median(apply_s);
    r.apply_msgs_per_s = static_cast<double>(r.messages) / median(apply_s);

    // ---- apply phase per venue (venue fed alone, file order) --------------------------------
    for (int v = 0; v < 2; ++v) {
        std::vector<double> secs;
        for (int k = 0; k < total_runs; ++k) {
            VenueSession s(v == 0 ? Venue::A : Venue::B);
            const auto t0 = Clock::now();
            for (const MsgRef& m : by_venue[v])
                for (std::uint32_t i = m.begin; i < m.end; ++i) s.on_event(evs[i]);
            const double sec = seconds_since(t0);
            if (s.counters() != ref[v]) r.counters_stable = false;
            if (k >= cfg.warmup) secs.push_back(sec);
        }
        const double med = median(secs);
        r.apply_events_per_s_by_venue[v] = med > 0 ? static_cast<double>(r.events_by_venue[v]) / med : 0.0;
    }

    // ---- latency phase ----------------------------------------------------------------------
    std::vector<std::int64_t> lat[2];
    for (int k = 0; k < total_runs; ++k) {
        VenueSession sa(Venue::A), sb(Venue::B);
        VenueSession* const ss[2] = {&sa, &sb};
        const bool timed = k >= cfg.warmup;
        for (const MsgRef& m : order) {
            VenueSession& s = *ss[m.venue];
            if (timed && is_update(evs[m.begin].type)) {
                const auto t0 = Clock::now();
                for (std::uint32_t i = m.begin; i < m.end; ++i) s.on_event(evs[i]);
                const auto t1 = Clock::now();
                lat[m.venue].push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
            } else {
                for (std::uint32_t i = m.begin; i < m.end; ++i) s.on_event(evs[i]);
            }
        }
        check(sa.counters(), sb.counters());
    }
    std::vector<std::int64_t> pooled;
    pooled.reserve(lat[0].size() + lat[1].size());
    for (int v = 0; v < 2; ++v) {
        pooled.insert(pooled.end(), lat[v].begin(), lat[v].end());
        std::sort(lat[v].begin(), lat[v].end());
        r.latency_samples_by_venue[v] = static_cast<std::int64_t>(lat[v].size());
        r.update_p50_by_venue[v] = percentile_permille(lat[v], 500);
        r.update_p99_by_venue[v] = percentile_permille(lat[v], 990);
        r.update_p999_by_venue[v] = percentile_permille(lat[v], 999);
    }
    std::sort(pooled.begin(), pooled.end());
    r.latency_samples = static_cast<std::int64_t>(pooled.size());
    r.update_latency_p50_ns = percentile_permille(pooled, 500);
    r.update_latency_p99_ns = percentile_permille(pooled, 990);
    r.update_latency_p999_ns = percentile_permille(pooled, 999);

    // ---- end to end (parse + sessions + merge, counting sink) -------------------------------
    std::vector<double> e2e_s;
    std::int64_t e2e_msgs = 0, e2e_events = 0;
    for (int k = 0; k < total_runs; ++k) {
        std::int64_t quotes = 0;
        const auto t0 = Clock::now();
        const MergeSummary s = run_merged(a, b, [&quotes](const MergedQuote&) { ++quotes; });
        const double sec = seconds_since(t0);
        if (quotes != s.merged_quotes) r.counters_stable = false;
        VenueCounters ca = s.counters[0], cb = s.counters[1];
        check(ca, cb);
        e2e_msgs = ca.messages + cb.messages;
        e2e_events = ca.events + cb.events;
        if (k >= cfg.warmup) e2e_s.push_back(sec);
    }
    const double e2e_med = median(e2e_s);
    r.e2e_msgs_per_s = static_cast<double>(e2e_msgs) / e2e_med;
    r.e2e_events_per_s = static_cast<double>(e2e_events) / e2e_med;

    // ---- timer overhead ---------------------------------------------------------------------
    std::vector<std::int64_t> ov(1000000);
    for (auto& x : ov) {
        const auto t0 = Clock::now();
        const auto t1 = Clock::now();
        x = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    }
    std::nth_element(ov.begin(), ov.begin() + static_cast<std::ptrdiff_t>(ov.size() / 2), ov.end());
    r.timer_overhead_ns_p50 = ov[ov.size() / 2];
    return r;
}

std::string to_json(const BenchResult& r) {
    std::ostringstream os;
    os << "{\"parse_events_per_s\":" << fmt(r.parse_events_per_s)
       << ",\"apply_events_per_s\":" << fmt(r.apply_events_per_s)
       << ",\"apply_msgs_per_s\":" << fmt(r.apply_msgs_per_s)
       << ",\"update_latency_p50_ns\":" << r.update_latency_p50_ns
       << ",\"update_latency_p99_ns\":" << r.update_latency_p99_ns
       << ",\"update_latency_p999_ns\":" << r.update_latency_p999_ns
       << ",\"latency_samples\":" << r.latency_samples << ",\"e2e_msgs_per_s\":" << fmt(r.e2e_msgs_per_s)
       << ",\"e2e_events_per_s\":" << fmt(r.e2e_events_per_s)
       << ",\"timer_overhead_ns_p50\":" << r.timer_overhead_ns_p50 << ",\"messages\":" << r.messages
       << ",\"events\":" << r.events << ",\"reps\":" << r.reps << ",\"warmup\":" << r.warmup
       << ",\"build\":\"" << json_escape(r.build) << "\"";
    // Additive keys (after the section 10 keys).
    static const char* const names[2] = {"A", "B"};
    for (int v = 0; v < 2; ++v) {
        os << ",\"" << names[v] << "\":{\"messages\":" << r.messages_by_venue[v]
           << ",\"events\":" << r.events_by_venue[v]
           << ",\"apply_events_per_s\":" << fmt(r.apply_events_per_s_by_venue[v])
           << ",\"latency_samples\":" << r.latency_samples_by_venue[v]
           << ",\"update_latency_p50_ns\":" << r.update_p50_by_venue[v]
           << ",\"update_latency_p99_ns\":" << r.update_p99_by_venue[v]
           << ",\"update_latency_p999_ns\":" << r.update_p999_by_venue[v] << '}';
    }
    os << ",\"parse_errors\":" << r.parse_errors << ",\"counters_stable\":"
       << (r.counters_stable ? "true" : "false") << '}';
    return os.str();
}

std::string to_text(const BenchResult& r) {
    std::ostringstream os;
    os << "build: " << r.build << '\n'
       << "feeds: " << r.messages << " messages, " << r.events << " events (A " << r.messages_by_venue[0]
       << " msgs / " << r.events_by_venue[0] << " events, B " << r.messages_by_venue[1] << " msgs / "
       << r.events_by_venue[1] << " events), parse_errors=" << r.parse_errors << '\n'
       << "reps: " << r.reps << " timed, " << r.warmup << " warm-up (throughput = median of timed reps)\n"
       << "parse           : " << fmt(r.parse_events_per_s) << " events/s\n"
       << "apply (merged)  : " << fmt(r.apply_events_per_s) << " events/s, " << fmt(r.apply_msgs_per_s)
       << " msgs/s\n"
       << "apply venue A   : " << fmt(r.apply_events_per_s_by_venue[0]) << " events/s (fed alone)\n"
       << "apply venue B   : " << fmt(r.apply_events_per_s_by_venue[1]) << " events/s (fed alone)\n"
       << "end-to-end      : " << fmt(r.e2e_events_per_s) << " events/s, " << fmt(r.e2e_msgs_per_s)
       << " msgs/s (parse + sessions + merge + latency summary)\n";
    static const char* const names[2] = {"A", "B"};
    for (int v = 0; v < 2; ++v)
        os << "update latency " << names[v] << " : p50 " << r.update_p50_by_venue[v] << " ns, p99 "
           << r.update_p99_by_venue[v] << " ns, p99.9 " << r.update_p999_by_venue[v] << " ns ("
           << r.latency_samples_by_venue[v] << " samples)\n";
    os << "update latency all: p50 " << r.update_latency_p50_ns << " ns, p99 " << r.update_latency_p99_ns
       << " ns, p99.9 " << r.update_latency_p999_ns << " ns (" << r.latency_samples << " samples)\n"
       << "timer overhead  : p50 " << r.timer_overhead_ns_p50
       << " ns per steady_clock::now() pair (reported, not subtracted)\n"
       << "counters stable across reps: " << (r.counters_stable ? "yes" : "NO") << '\n';
    return os.str();
}

}  // namespace md
