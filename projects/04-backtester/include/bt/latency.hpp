// Latency model with FIFO channels and optional seeded jitter (DESIGN.md sections 2.3 and 6).
#pragma once

#include <cstdint>

namespace bt {

class SplitMix64 {
public:
    explicit SplitMix64(std::uint64_t seed) : state_(seed) {}
    std::uint64_t next();
    double uniform01();  // (next() >> 11) * 2^-53, in [0, 1)

private:
    std::uint64_t state_;
};

struct LatencyConfig {
    std::int64_t entry_ns = 0, cancel_ns = 0, ack_ns = 0, jitter_mean_ns = 0;
    std::uint64_t seed = 1;
};

class LatencyModel {
public:
    explicit LatencyModel(const LatencyConfig& c);
    // strategy -> exchange channel, shared by orders and cancels (a cancel never overtakes its order)
    std::int64_t entry_arrival(std::int64_t send_ns);
    std::int64_t cancel_arrival(std::int64_t send_ns);
    // exchange -> strategy channel
    std::int64_t report_arrival(std::int64_t exch_ns);
    std::int64_t draws() const { return draws_; }  // number of jitter draws made so far

private:
    std::int64_t jitter();
    LatencyConfig c_;
    SplitMix64 rng_;
    std::int64_t last_in_, last_out_;
    std::int64_t draws_ = 0;
};

}  // namespace bt
