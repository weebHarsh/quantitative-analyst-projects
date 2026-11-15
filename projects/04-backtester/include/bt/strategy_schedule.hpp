// Fill-independent order schedule used by the model-ordering test (DESIGN.md 4.3).
#pragma once

#include <cstdint>

#include "bt/strategy.hpp"

namespace bt {

struct ScheduleParams {
    std::int64_t period_ns = 500'000'000, life_ns = 400'000'000, qty_lots = 1000;
};

// Timer ids: 0 = schedule tick (even); 2*order_id+1 (odd) cancels that order.
class ScheduledQuoter : public Strategy {
public:
    explicit ScheduledQuoter(ScheduleParams p = {});
    void on_market(const MarketView& v, StrategyContext& ctx) override;
    void on_report(const Report& r, const MarketView& v, StrategyContext& ctx) override;
    void on_timer(std::int64_t timer_id, const MarketView& v, StrategyContext& ctx) override;

private:
    ScheduleParams p_;
    bool started_ = false;
};

}  // namespace bt
