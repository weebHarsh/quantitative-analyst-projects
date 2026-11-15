#include "bt/latency.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace bt {

std::uint64_t SplitMix64::next() {
    std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

double SplitMix64::uniform01() {
    return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0);  // 2^-53
}

LatencyModel::LatencyModel(const LatencyConfig& c)
    : c_(c),
      rng_(c.seed),
      last_in_(std::numeric_limits<std::int64_t>::min()),
      last_out_(std::numeric_limits<std::int64_t>::min()) {}

std::int64_t LatencyModel::jitter() {
    if (c_.jitter_mean_ns <= 0) return 0;  // no draw
    ++draws_;
    const double u = rng_.uniform01();
    return static_cast<std::int64_t>(std::floor(-static_cast<double>(c_.jitter_mean_ns) * std::log1p(-u)));
}

std::int64_t LatencyModel::entry_arrival(std::int64_t send_ns) {
    const std::int64_t t = std::max(send_ns + c_.entry_ns + jitter(), last_in_);
    last_in_ = t;
    return t;
}

std::int64_t LatencyModel::cancel_arrival(std::int64_t send_ns) {
    const std::int64_t t = std::max(send_ns + c_.cancel_ns + jitter(), last_in_);
    last_in_ = t;
    return t;
}

std::int64_t LatencyModel::report_arrival(std::int64_t exch_ns) {
    const std::int64_t t = std::max(exch_ns + c_.ack_ns + jitter(), last_out_);
    last_out_ = t;
    return t;
}

}  // namespace bt
