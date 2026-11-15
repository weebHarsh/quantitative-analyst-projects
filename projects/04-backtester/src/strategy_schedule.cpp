#include "bt/strategy_schedule.hpp"

#include <algorithm>

namespace bt {

ScheduledQuoter::ScheduledQuoter(ScheduleParams p) : p_(p) {}

void ScheduledQuoter::on_market(const MarketView& v, StrategyContext& ctx) {
    if (started_ || !v.live) return;
    started_ = true;
    ctx.set_timer(ctx.now_ns() + p_.period_ns, 0);
}

void ScheduledQuoter::on_report(const Report&, const MarketView&, StrategyContext&) {}

void ScheduledQuoter::on_timer(std::int64_t timer_id, const MarketView& v, StrategyContext& ctx) {
    if ((timer_id & 1) != 0) {  // odd: cancel order (timer_id - 1) / 2 whatever its state
        ctx.send_cancel((timer_id - 1) / 2);
        return;
    }
    const std::int64_t now = ctx.now_ns();
    if (v.has_bid) {
        const OrderId id = ctx.send_order(Side::Bid, v.bid_px, p_.qty_lots);
        if (id >= 1) ctx.set_timer(now + p_.life_ns, 2 * id + 1);
    }
    if (v.has_ask) {
        const OrderId id = ctx.send_order(Side::Ask, v.ask_px, p_.qty_lots);
        if (id >= 1) ctx.set_timer(now + p_.life_ns, 2 * id + 1);
    }
    // A period of 0 (or less) would re-arm at the same instant forever and exhaust memory.
    // bt_run requires at least 1 microsecond; the library only guarantees time advances (1 ns).
    ctx.set_timer(now + std::max<std::int64_t>(p_.period_ns, 1), 0);
}

}  // namespace bt
