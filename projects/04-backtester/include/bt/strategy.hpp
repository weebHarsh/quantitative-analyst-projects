// Strategy port (DESIGN.md section 4.1).
#pragma once

#include <cstdint>

#include "bt/types.hpp"
#include "md/book.hpp"

namespace bt {

struct MarketView {  // the market book only (no own orders)
    std::int64_t ts_ns = 0;
    bool live = false;  // session.state() == Live
    bool has_bid = false, has_ask = false;
    std::int64_t bid_px = 0, bid_qty = 0, ask_px = 0, ask_qty = 0;
    const md::Book* book = nullptr;  // full depth, read-only
};

class StrategyContext {
public:
    virtual ~StrategyContext() = default;
    virtual std::int64_t now_ns() const = 0;
    // Post-only limit order; id >= 1 and increasing. -1 on invalid arguments (release builds).
    virtual OrderId send_order(md::Side side, std::int64_t px_ticks, std::int64_t qty_lots) = 0;
    virtual void send_cancel(OrderId id) = 0;
    virtual void set_timer(std::int64_t at_ns, std::int64_t timer_id) = 0;  // at_ns < now -> now
};

class Strategy {
public:
    virtual ~Strategy() = default;
    virtual void on_market(const MarketView& v, StrategyContext& ctx) = 0;  // after every market message
    virtual void on_report(const Report& r, const MarketView& v, StrategyContext& ctx) = 0;
    virtual void on_timer(std::int64_t timer_id, const MarketView& v, StrategyContext& ctx) {
        (void)timer_id;
        (void)v;
        (void)ctx;
    }
};

}  // namespace bt
