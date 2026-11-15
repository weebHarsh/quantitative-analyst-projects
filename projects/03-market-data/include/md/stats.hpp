// Latency statistics (apparent latency = local_ts - exch_ts).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace md {

struct LatencySummary {
    std::int64_t count = 0, sum_ns = 0, p50_ns = 0, p99_ns = 0, min_ns = 0, max_ns = 0;
    double mean_ns = 0.0;
};

// Nearest rank: s[max((pct*n + 99) / 100 - 1, 0)]; empty -> 0.
std::int64_t nearest_rank(const std::vector<std::int64_t>& sorted, int pct);

class LatencyStats {
public:
    void reserve(std::size_t n);
    void add(std::int64_t ns);
    LatencySummary summary() const;  // sorts a copy
private:
    std::vector<std::int64_t> samples_;
};

}  // namespace md
