#include "md/stats.hpp"
#include "md/saturate.hpp"

#include <algorithm>

namespace md {

std::int64_t nearest_rank(const std::vector<std::int64_t>& sorted, int pct) {
    if (sorted.empty()) return 0;
    const std::size_t n = sorted.size();
    const std::size_t q = static_cast<std::size_t>(pct);
    const std::size_t rank = (q * n + 99) / 100;
    const std::size_t idx = rank > 0 ? rank - 1 : 0;
    return sorted[std::min(idx, n - 1)];
}

void LatencyStats::reserve(std::size_t n) { samples_.reserve(n); }
void LatencyStats::add(std::int64_t ns) { samples_.push_back(ns); }

LatencySummary LatencyStats::summary() const {
    LatencySummary s;
    if (samples_.empty()) return s;
    std::vector<std::int64_t> v = samples_;
    std::sort(v.begin(), v.end());
    s.count = static_cast<std::int64_t>(v.size());
    for (std::int64_t x : v) s.sum_ns = sat_add(s.sum_ns, x);
    s.mean_ns = static_cast<double>(s.sum_ns) / static_cast<double>(s.count);
    s.p50_ns = nearest_rank(v, 50);
    s.p99_ns = nearest_rank(v, 99);
    s.min_ns = v.front();
    s.max_ns = v.back();
    return s;
}

}  // namespace md
