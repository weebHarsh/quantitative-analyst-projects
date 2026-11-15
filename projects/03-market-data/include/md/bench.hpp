// Benchmark driver (DESIGN.md section 10).
#pragma once

#include <cstdint>
#include <string>

#include "md/io.hpp"

namespace md {

struct BenchConfig {
    int reps = 5;
    int warmup = 1;
};

struct BenchResult {
    std::int64_t messages = 0, events = 0, latency_samples = 0;
    double parse_events_per_s = 0, apply_events_per_s = 0, apply_msgs_per_s = 0, e2e_msgs_per_s = 0,
           e2e_events_per_s = 0;
    std::int64_t update_latency_p50_ns = 0, update_latency_p99_ns = 0, update_latency_p999_ns = 0,
                 timer_overhead_ns_p50 = 0;
    int reps = 0, warmup = 0;
    std::string build;
    // Additive: per-venue breakdown and run health.
    std::int64_t events_by_venue[2] = {0, 0}, messages_by_venue[2] = {0, 0};
    std::int64_t latency_samples_by_venue[2] = {0, 0};
    std::int64_t update_p50_by_venue[2] = {0, 0}, update_p99_by_venue[2] = {0, 0},
                 update_p999_by_venue[2] = {0, 0};
    double apply_events_per_s_by_venue[2] = {0, 0};  // venue fed alone, file order
    std::int64_t parse_errors = 0;
    bool counters_stable = true;  // false: a rep's counters differed from the first (exit 6)
};

BenchResult run_bench(const Feed& a, const Feed& b, const BenchConfig& cfg);
std::string to_json(const BenchResult& r);
// Human-readable multi-line summary (also machine-readable "key=value" lines).
std::string to_text(const BenchResult& r);

}  // namespace md
