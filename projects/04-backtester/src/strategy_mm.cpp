#include "bt/strategy_mm.hpp"

#include <algorithm>

namespace bt {

MarketMaker::MarketMaker(MMParams p) : p_(p) {}

MarketMaker::Own* MarketMaker::find(OrderId id) {
    for (Own& o : orders_)
        if (o.id == id) return &o;
    return nullptr;
}

MarketMaker::Own* MarketMaker::working(Side s) {
    for (Own& o : orders_)
        if (o.side == s && (o.state == State::PendingNew || o.state == State::Live)) return &o;
    return nullptr;
}

void MarketMaker::remove(OrderId id) {
    orders_.erase(std::remove_if(orders_.begin(), orders_.end(), [id](const Own& o) { return o.id == id; }),
                  orders_.end());
}

void MarketMaker::on_market(const MarketView& v, StrategyContext& ctx) { decide(v, ctx); }

void MarketMaker::on_report(const Report& r, const MarketView& v, StrategyContext& ctx) {
    switch (r.type) {
        case ReportType::Accepted:
            if (Own* o = find(r.id); o != nullptr && o->state == State::PendingNew) o->state = State::Live;
            break;
        case ReportType::Rejected:
        case ReportType::Canceled:
        case ReportType::CancelRejected:
            remove(r.id);
            break;
        case ReportType::Filled:
            position_ += r.side == Side::Bid ? r.qty_lots : -r.qty_lots;
            if (r.remaining_lots == 0) {
                remove(r.id);
            } else if (Own* o = find(r.id); o != nullptr) {
                o->remaining = r.remaining_lots;
            }
            break;
    }
    decide(v, ctx);
}

void MarketMaker::decide(const MarketView& v, StrategyContext& ctx) {
    for (Side side : {Side::Bid, Side::Ask}) {
        bool want = false;
        std::int64_t d = 0;
        if (side == Side::Bid) {
            if (v.live && v.has_bid && position_ < p_.max_inv_lots) {
                want = true;
                d = v.bid_px - (position_ > p_.skew_inv_lots ? p_.skew_ticks : 0);
            }
        } else {
            if (v.live && v.has_ask && position_ > -p_.max_inv_lots) {
                want = true;
                d = v.ask_px + (position_ < -p_.skew_inv_lots ? p_.skew_ticks : 0);
            }
        }
        Own* w = working(side);
        if (!want) {
            if (w != nullptr) {
                ctx.send_cancel(w->id);
                w->state = State::PendingCancel;
            }
        } else if (w == nullptr || w->px != d) {
            if (w != nullptr) {
                ctx.send_cancel(w->id);
                w->state = State::PendingCancel;
            }
            const OrderId id = ctx.send_order(side, d, p_.quote_qty_lots);
            if (id >= 1) {
                Own o;
                o.id = id;
                o.side = side;
                o.px = d;
                o.remaining = p_.quote_qty_lots;
                orders_.push_back(o);
            }
        }
    }
}

}  // namespace bt
