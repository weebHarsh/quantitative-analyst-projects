// Section 9.2 S7: market maker rules with a fake context (no event loop), plus the schedule strategy.
#include <string>
#include <vector>

#include "bt/strategy_mm.hpp"
#include "bt/strategy_schedule.hpp"
#include "harness.hpp"

using bt::ReportType;
using md::Side;

namespace {

struct Act {
    enum Kind { Send, Cancel, Timer } kind;
    Side side = Side::None;
    std::int64_t a = 0, b = 0;  // Send: px, qty. Cancel: id. Timer: at, timer_id
};

class FakeCtx : public bt::StrategyContext {
public:
    std::vector<Act> acts;
    std::int64_t now = 1000;
    bt::OrderId next = 1;
    std::int64_t now_ns() const override { return now; }
    bt::OrderId send_order(Side s, std::int64_t px, std::int64_t qty) override {
        acts.push_back({Act::Send, s, px, qty});
        return next++;
    }
    void send_cancel(bt::OrderId id) override { acts.push_back({Act::Cancel, Side::None, id, 0}); }
    void set_timer(std::int64_t at, std::int64_t id) override { acts.push_back({Act::Timer, Side::None, at, id}); }
    void clear() { acts.clear(); }
};

bt::MarketView view(std::int64_t bid, std::int64_t ask, bool live = true) {
    bt::MarketView v;
    v.live = live;
    v.has_bid = live;
    v.has_ask = live;
    v.bid_px = bid;
    v.ask_px = ask;
    v.bid_qty = 1000;
    v.ask_qty = 1000;
    return v;
}

bt::Report report(ReportType t, bt::OrderId id, Side s, std::int64_t px, std::int64_t qty = 0,
                  std::int64_t rem = 0) {
    bt::Report r;
    r.type = t;
    r.id = id;
    r.side = s;
    r.px_ticks = px;
    r.qty_lots = qty;
    r.remaining_lots = rem;
    return r;
}

bool is(const Act& a, Act::Kind k, Side s, std::int64_t x, std::int64_t y) {
    return a.kind == k && a.side == s && a.a == x && a.b == y;
}
bool is_cancel(const Act& a, std::int64_t id) { return a.kind == Act::Cancel && a.a == id; }

}  // namespace

TEST(mm_flat_joins_both_sides_of_the_touch) {
    bt::MarketMaker mm;
    FakeCtx c;
    mm.on_market(view(100, 101), c);
    CHECK_EQ(c.acts.size(), static_cast<std::size_t>(2));
    if (c.acts.size() == 2) {
        CHECK(is(c.acts[0], Act::Send, Side::Bid, 100, 1000));  // Bid first
        CHECK(is(c.acts[1], Act::Send, Side::Ask, 101, 1000));
    }
    CHECK_EQ(mm.position_lots(), 0);
}

TEST(mm_pending_new_order_is_not_duplicated) {
    bt::MarketMaker mm;
    FakeCtx c;
    mm.on_market(view(100, 101), c);
    c.clear();
    mm.on_market(view(100, 101), c);
    CHECK(c.acts.empty());
    mm.on_report(report(ReportType::Accepted, 1, Side::Bid, 100), view(100, 101), c);
    mm.on_report(report(ReportType::Accepted, 2, Side::Ask, 101), view(100, 101), c);
    CHECK(c.acts.empty());
}

TEST(mm_inventory_skew_is_strict_and_pull_at_limit) {
    {
        bt::MarketMaker mm;
        FakeCtx c;
        mm.on_market(view(100, 101), c);
        c.clear();
        // exactly skew_inv: no skew yet (strict comparison)
        mm.on_report(report(ReportType::Filled, 1, Side::Bid, 100, 20000, 0), view(100, 101), c);
        CHECK_EQ(mm.position_lots(), 20000);
        CHECK_EQ(c.acts.size(), static_cast<std::size_t>(1));  // order 1 is gone: re-quote at the touch
        if (!c.acts.empty()) CHECK(is(c.acts[0], Act::Send, Side::Bid, 100, 1000));
    }
    {
        bt::MarketMaker mm;
        FakeCtx c;
        mm.on_market(view(100, 101), c);
        c.clear();
        mm.on_report(report(ReportType::Filled, 1, Side::Bid, 100, 20001, 0), view(100, 101), c);
        CHECK_EQ(c.acts.size(), static_cast<std::size_t>(1));
        if (!c.acts.empty()) CHECK(is(c.acts[0], Act::Send, Side::Bid, 99, 1000));  // one tick behind
        // the ask side is not skewed
    }
    {
        bt::MarketMaker mm;
        FakeCtx c;
        mm.on_market(view(100, 101), c);
        c.clear();
        mm.on_report(report(ReportType::Filled, 2, Side::Ask, 101, 20001, 0), view(100, 101), c);  // position -20001
        CHECK_EQ(mm.position_lots(), -20001);
        CHECK_EQ(c.acts.size(), static_cast<std::size_t>(1));
        if (!c.acts.empty()) CHECK(is(c.acts[0], Act::Send, Side::Ask, 102, 1000));
    }
}

TEST(mm_position_at_max_pulls_and_cancels_the_working_bid) {
    bt::MarketMaker mm;
    FakeCtx c;
    mm.on_market(view(100, 101), c);
    c.clear();
    // a partial fill of 50000 lots leaves order 1 working with 1000 lots: position hits max_inv
    mm.on_report(report(ReportType::Filled, 1, Side::Bid, 100, 50000, 1000), view(100, 101), c);
    CHECK_EQ(mm.position_lots(), 50000);
    CHECK_EQ(c.acts.size(), static_cast<std::size_t>(1));
    if (!c.acts.empty()) CHECK(is_cancel(c.acts[0], 1));
    // while the cancel is pending nothing else is sent, even on new market data
    c.clear();
    mm.on_market(view(100, 101), c);
    CHECK(c.acts.empty());
    // the cancel outcome removes it; the bid stays pulled
    mm.on_report(report(ReportType::Canceled, 1, Side::Bid, 100, 1000, 0), view(100, 101), c);
    CHECK(c.acts.empty());
    // below the limit the bid comes back (skewed: position > skew_inv)
    mm.on_report(report(ReportType::Filled, 2, Side::Ask, 101, 1000, 0), view(100, 101), c);
    CHECK_EQ(mm.position_lots(), 49000);
    bool saw_bid_99 = false;
    for (const Act& a : c.acts) saw_bid_99 = saw_bid_99 || is(a, Act::Send, Side::Bid, 99, 1000);
    CHECK(saw_bid_99);
}

TEST(mm_requote_when_the_touch_moves_cancel_first_then_new_order) {
    bt::MarketMaker mm;
    FakeCtx c;
    mm.on_market(view(100, 101), c);
    mm.on_report(report(ReportType::Accepted, 1, Side::Bid, 100), view(100, 101), c);
    mm.on_report(report(ReportType::Accepted, 2, Side::Ask, 101), view(100, 101), c);
    c.clear();
    mm.on_market(view(101, 102), c);  // touch moves up a tick
    CHECK_EQ(c.acts.size(), static_cast<std::size_t>(4));
    if (c.acts.size() == 4) {
        CHECK(is_cancel(c.acts[0], 1));
        CHECK(is(c.acts[1], Act::Send, Side::Bid, 101, 1000));
        CHECK(is_cancel(c.acts[2], 2));
        CHECK(is(c.acts[3], Act::Send, Side::Ask, 102, 1000));
    }
    // another tick of data at the new touch: the replacement orders are PendingNew, nothing to do
    c.clear();
    mm.on_market(view(101, 102), c);
    CHECK(c.acts.empty());
}

TEST(mm_after_a_rejection_it_sends_again) {
    bt::MarketMaker mm;
    FakeCtx c;
    mm.on_market(view(100, 101), c);
    c.clear();
    bt::Report r = report(ReportType::Rejected, 1, Side::Bid, 100);
    r.reason = bt::RejectReason::WouldCross;
    mm.on_report(r, view(99, 101), c);
    CHECK_EQ(c.acts.size(), static_cast<std::size_t>(1));
    if (!c.acts.empty()) CHECK(is(c.acts[0], Act::Send, Side::Bid, 99, 1000));
}

TEST(mm_partially_filled_order_is_not_topped_up) {
    bt::MarketMaker mm;
    FakeCtx c;
    mm.on_market(view(100, 101), c);
    mm.on_report(report(ReportType::Accepted, 1, Side::Bid, 100), view(100, 101), c);
    c.clear();
    mm.on_report(report(ReportType::Filled, 1, Side::Bid, 100, 400, 600), view(100, 101), c);
    CHECK_EQ(mm.position_lots(), 400);
    CHECK(c.acts.empty());
    // the remainder fills: now the side is re-quoted
    mm.on_report(report(ReportType::Filled, 1, Side::Bid, 100, 600, 0), view(100, 101), c);
    CHECK_EQ(mm.position_lots(), 1000);
    CHECK_EQ(c.acts.size(), static_cast<std::size_t>(1));
    if (!c.acts.empty()) CHECK(is(c.acts[0], Act::Send, Side::Bid, 100, 1000));
}

TEST(mm_not_live_cancels_everything_once) {
    bt::MarketMaker mm;
    FakeCtx c;
    mm.on_market(view(100, 101), c);
    mm.on_report(report(ReportType::Accepted, 1, Side::Bid, 100), view(100, 101), c);
    c.clear();
    mm.on_market(view(0, 0, false), c);
    CHECK_EQ(c.acts.size(), static_cast<std::size_t>(2));
    if (c.acts.size() == 2) {
        CHECK(is_cancel(c.acts[0], 1));
        CHECK(is_cancel(c.acts[1], 2));
    }
    c.clear();
    mm.on_market(view(0, 0, false), c);  // already PendingCancel
    CHECK(c.acts.empty());
    // cancel outcomes arrive while still not live: nothing is sent
    mm.on_report(report(ReportType::Canceled, 1, Side::Bid, 100, 1000, 0), view(0, 0, false), c);
    mm.on_report(report(ReportType::CancelRejected, 2, Side::Ask, 101), view(0, 0, false), c);
    CHECK(c.acts.empty());
    // back live: quotes again
    mm.on_market(view(100, 101), c);
    CHECK_EQ(c.acts.size(), static_cast<std::size_t>(2));
}

TEST(mm_one_sided_book_quotes_only_that_side) {
    bt::MarketMaker mm;
    FakeCtx c;
    bt::MarketView v = view(100, 101);
    v.has_ask = false;
    mm.on_market(v, c);
    CHECK_EQ(c.acts.size(), static_cast<std::size_t>(1));
    if (!c.acts.empty()) CHECK(is(c.acts[0], Act::Send, Side::Bid, 100, 1000));
}

TEST(schedule_quoter_ticks_and_cancel_timers) {
    bt::ScheduleParams p;
    p.period_ns = 500;
    p.life_ns = 400;
    p.qty_lots = 777;
    bt::ScheduledQuoter q(p);
    FakeCtx c;
    c.now = 1000;
    q.on_market(view(0, 0, false), c);  // not live: nothing
    CHECK(c.acts.empty());
    q.on_market(view(100, 101), c);
    CHECK_EQ(c.acts.size(), static_cast<std::size_t>(1));
    if (!c.acts.empty()) CHECK(c.acts[0].kind == Act::Timer && c.acts[0].a == 1500 && c.acts[0].b == 0);
    c.clear();
    q.on_market(view(100, 101), c);  // only the first live call schedules
    CHECK(c.acts.empty());
    c.now = 1500;
    q.on_timer(0, view(100, 101), c);
    CHECK_EQ(c.acts.size(), static_cast<std::size_t>(5));
    if (c.acts.size() == 5) {
        CHECK(is(c.acts[0], Act::Send, Side::Bid, 100, 777));
        CHECK(c.acts[1].kind == Act::Timer && c.acts[1].a == 1900 && c.acts[1].b == 3);  // 2*1+1
        CHECK(is(c.acts[2], Act::Send, Side::Ask, 101, 777));
        CHECK(c.acts[3].kind == Act::Timer && c.acts[3].a == 1900 && c.acts[3].b == 5);  // 2*2+1
        CHECK(c.acts[4].kind == Act::Timer && c.acts[4].a == 2000 && c.acts[4].b == 0);
    }
    c.clear();
    q.on_timer(5, view(100, 101), c);  // cancel timer of order 2
    CHECK_EQ(c.acts.size(), static_cast<std::size_t>(1));
    if (!c.acts.empty()) CHECK(is_cancel(c.acts[0], 2));
}
