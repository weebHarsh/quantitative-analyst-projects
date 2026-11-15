// Port of Project 1's market maker (pta.strategy.market_maker) to the real book (DESIGN.md 4.2).
#pragma once

#include <cstdint>
#include <vector>

#include "bt/strategy.hpp"

namespace bt {

struct MMParams {
    std::int64_t quote_qty_lots = 1000, skew_inv_lots = 20000, max_inv_lots = 50000, skew_ticks = 1;
};

class MarketMaker : public Strategy {
public:
    explicit MarketMaker(MMParams p = {});
    std::int64_t position_lots() const { return position_; }  // as known to the strategy (Filled reports)
    void on_market(const MarketView& v, StrategyContext& ctx) override;
    void on_report(const Report& r, const MarketView& v, StrategyContext& ctx) override;

private:
    enum class State : std::uint8_t { PendingNew, Live, PendingCancel };
    struct Own {
        OrderId id = 0;
        Side side = Side::None;
        std::int64_t px = 0, remaining = 0;
        State state = State::PendingNew;
    };
    Own* find(OrderId id);
    Own* working(Side s);
    void remove(OrderId id);
    void decide(const MarketView& v, StrategyContext& ctx);

    MMParams p_;
    std::int64_t position_ = 0;
    std::vector<Own> orders_;
};

}  // namespace bt
