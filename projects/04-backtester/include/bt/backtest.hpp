// Event-driven backtest of a strategy against one venue's normalised events (DESIGN.md sections 2-5).
#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "bt/latency.hpp"
#include "bt/strategy.hpp"
#include "bt/types.hpp"
#include "md/types.hpp"

namespace bt {

struct BacktestConfig {
    QueueConfig queue{};
    LatencyConfig latency{};
    bool cross_fills = true;  // section 3.5; false = ablation
    double fee_bps = 0.1;
};

// One member per summary.csv key (section 5.5), same names, in file order.
struct RunSummary {
    std::string model, strategy = "custom";
    std::int64_t cancel_ahead_ppm = 0;
    bool cross_fills = true;
    std::int64_t entry_latency_ns = 0, cancel_latency_ns = 0, ack_latency_ns = 0, latency_jitter_mean_ns = 0;
    std::uint64_t seed = 1;
    double fee_bps = 0;
    std::string venue = "A";
    std::int64_t input_events = 0, input_messages = 0, input_trades = 0;
    std::string session_final_state;  // lower case: init / live / stale
    std::int64_t session_gaps = 0, session_resyncs = 0, market_rows = 0, market_first_ts_us = 0,
                 market_last_ts_us = 0;
    std::int64_t orders_sent = 0, orders_accepted = 0, orders_rejected_would_cross = 0,
                 orders_rejected_not_live = 0, cancels_sent = 0, cancels_effective = 0, cancels_too_late = 0,
                 cancels_unknown = 0, cancels_in_flight_filled = 0;
    std::int64_t fills = 0;  // rows in fills.csv: completed by write_outputs
    std::int64_t fills_trade_at = 0, fills_trade_through = 0, fills_cross = 0, filled_lots = 0,
                 orders_with_fill = 0;
    double fill_rate = 0;  // orders_with_fill / orders_accepted
    std::int64_t final_position_lots = 0, max_abs_position_lots = 0, orders_open_at_end = 0,
                 trade_credit_unmatched_lots = 0;
    std::int64_t fills_outside_market = 0;  // completed by write_outputs
    std::uint64_t fills_fnv1a64 = 0;        // completed by write_outputs
};

struct BacktestResult {
    std::vector<Fill> fills;
    std::vector<OrderRecord> orders;
    RunSummary summary;
    std::int64_t market_first_ts_us = 0, market_last_ts_us = 0;
};

// events must pass group_messages (else returns false + err). market_out may be null; when set it
// receives market.csv (section 5.1). summary.fills, fills_outside_market and fills_fnv1a64 are left 0
// here; write_outputs completes them.
bool run_backtest(const std::vector<md::Event>& events, Strategy& strategy, const BacktestConfig& cfg,
                  std::ostream* market_out, BacktestResult& out, std::string& err);

}  // namespace bt
