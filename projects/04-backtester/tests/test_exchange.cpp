// Section 9.2: exchange and loop tests on hand-built feeds with a ScriptedStrategy.
#include <algorithm>
#include <string>
#include <tuple>
#include <vector>

#include "bt/queue.hpp"
#include "feed_builder.hpp"
#include "harness.hpp"
#include "scripted.hpp"

using bt::FillKind;
using bt::QueueModel;
using bt::ReportType;
using fb::ms;
using fb::us;
using md::Side;

namespace {

using FillExp = std::tuple<std::int64_t, std::int64_t, FillKind>;  // ts, lots, kind

void expect_fills(const sc::RunOut& r, const std::vector<FillExp>& exp) {
    CHECK(r.ok);
    CHECK_EQ(r.res.fills.size(), exp.size());
    for (std::size_t i = 0; i < exp.size() && i < r.res.fills.size(); ++i) {
        CHECK_EQ(r.res.fills[i].ts_ns, std::get<0>(exp[i]));
        CHECK_EQ(r.res.fills[i].qty_lots, std::get<1>(exp[i]));
        CHECK(r.res.fills[i].kind == std::get<2>(exp[i]));
    }
}

std::int64_t filled_by(const sc::RunOut& r, std::int64_t ts) {
    std::int64_t s = 0;
    for (const bt::Fill& f : r.res.fills)
        if (f.ts_ns <= ts) s += f.qty_lots;
    return s;
}

void expect_report(const bt::Report& r, ReportType t, std::int64_t ts, std::int64_t qty, std::int64_t rem,
                   std::int64_t exch_ts) {
    CHECK(r.type == t);
    CHECK_EQ(r.ts_ns, ts);
    CHECK_EQ(r.qty_lots, qty);
    CHECK_EQ(r.remaining_lots, rem);
    CHECK_EQ(r.exch_ts_ns, exch_ts);
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// S1: queue advancement in every model
// ---------------------------------------------------------------------------------------------

namespace {
std::vector<md::Event> s1_feed() {
    fb::Builder b;
    b.snapshot(ms(0), {{100, 500}, {99, 300}}, {{101, 400}});
    b.set(ms(2), Side::Bid, 100, 800);
    b.set(ms(3), Side::Bid, 100, 600);
    b.sell(ms(4), 100, 400);
    b.set(ms(4) + us(100), Side::Bid, 100, 200);
    b.sell(ms(5), 100, 150);
    b.set(ms(5) + us(100), Side::Bid, 100, 50);
    b.del(ms(6), Side::Bid, 100);
    b.sell(ms(7), 99, 300);
    b.del(ms(7) + us(100), Side::Bid, 99);
    return b.ev;
}

sc::RunOut run_s1(QueueModel m, std::int64_t ppm, bool cross = true) {
    sc::ScriptedStrategy s({sc::send(ms(1), Side::Bid, 100, 200)});
    return sc::run(s1_feed(), s, sc::make_cfg(m, ppm, 0, cross));
}

// The queue_ahead trajectory after t1..t6 computed with the pure functions only (spec table).
std::vector<std::int64_t> s1_queue_trajectory(const bt::QueueConfig& c) {
    std::int64_t q = bt::queue_on_arrival(c, 500), rem = 200;
    std::vector<std::int64_t> out{q};  // t1
    q = bt::queue_on_depth(c, q, 500, 800, 0);
    out.push_back(q);  // t2
    q = bt::queue_on_depth(c, q, 800, 600, 0);
    out.push_back(q);  // t3
    auto t = bt::queue_on_trade(c, q, rem, 400, false);
    rem -= t.fill;
    q = bt::queue_on_depth(c, t.queue_after, 600, 200, 400);
    out.push_back(q);  // t4
    t = bt::queue_on_trade(c, q, rem, 150, false);
    rem -= t.fill;
    q = bt::queue_on_depth(c, t.queue_after, 200, 50, 150);
    out.push_back(q);  // t5
    q = bt::queue_on_depth(c, q, 50, 0, 0);
    out.push_back(q);  // t6
    return out;
}
}  // namespace

TEST(s1_queue_trajectory_matches_spec_table) {
    using V = std::vector<std::int64_t>;
    CHECK((s1_queue_trajectory({QueueModel::Optimistic, 0}) == V{0, 0, 0, 0, 0, 0}));
    CHECK((s1_queue_trajectory({QueueModel::FifoBack, 0}) == V{500, 500, 500, 100, 0, 0}));
    CHECK((s1_queue_trajectory({QueueModel::FifoCancelAhead, 250000}) == V{500, 500, 450, 50, 0, 0}));
    CHECK((s1_queue_trajectory({QueueModel::FifoCancelAhead, 500000}) == V{500, 500, 400, 0, 0, 0}));
}

TEST(s1_optimistic) {
    const sc::RunOut r = run_s1(QueueModel::Optimistic, 0);
    expect_fills(r, {{ms(4), 200, FillKind::TradeAt}});
    CHECK_EQ(r.res.summary.trade_credit_unmatched_lots, 0);
    CHECK_EQ(r.res.orders.size(), static_cast<std::size_t>(1));
    CHECK_EQ(r.res.orders[0].queue_ahead_at_arrival, 0);
    CHECK(r.res.orders[0].status == bt::OrderStatus::Filled);
    CHECK_EQ(filled_by(r, ms(5)), 200);
}

TEST(s1_fifo_back) {
    const sc::RunOut r = run_s1(QueueModel::FifoBack, 0);
    expect_fills(r, {{ms(5), 50, FillKind::TradeAt}, {ms(7), 150, FillKind::TradeThrough}});
    CHECK_EQ(r.res.summary.trade_credit_unmatched_lots, 0);
    CHECK_EQ(r.res.orders[0].queue_ahead_at_arrival, 500);
    CHECK_EQ(r.res.fills[0].queue_ahead_before, 100);
    CHECK_EQ(r.res.fills[0].px_ticks, 100);
    CHECK_EQ(r.res.fills[1].px_ticks, 100);  // through fill is at OUR price, not the print's
    CHECK_EQ(r.res.fills[1].queue_ahead_before, 0);
    CHECK(r.res.fills[0].side == Side::Bid);
    CHECK_EQ(filled_by(r, ms(5)), 50);
    CHECK_EQ(r.res.summary.filled_lots, 200);
    CHECK_EQ(r.res.summary.fills_trade_at, 1);
    CHECK_EQ(r.res.summary.fills_trade_through, 1);
}

TEST(s1_fifo_cancel_ahead_quarter_and_half) {
    const sc::RunOut q = run_s1(QueueModel::FifoCancelAhead, 250000);
    expect_fills(q, {{ms(5), 100, FillKind::TradeAt}, {ms(7), 100, FillKind::TradeThrough}});
    CHECK_EQ(q.res.fills[0].queue_ahead_before, 50);
    CHECK_EQ(q.res.summary.trade_credit_unmatched_lots, 0);
    const sc::RunOut h = run_s1(QueueModel::FifoCancelAhead, 500000);
    expect_fills(h, {{ms(5), 150, FillKind::TradeAt}, {ms(7), 50, FillKind::TradeThrough}});
    CHECK_EQ(h.res.fills[0].queue_ahead_before, 0);
    CHECK_EQ(h.res.summary.trade_credit_unmatched_lots, 0);
}

TEST(s1_cancel_ahead_zero_equals_fifo_back_and_cumulative_ordering) {
    const sc::RunOut back = run_s1(QueueModel::FifoBack, 0);
    const sc::RunOut zero = run_s1(QueueModel::FifoCancelAhead, 0);
    CHECK_EQ(back.fills_csv, zero.fills_csv);
    CHECK_EQ(back.res.fills.size(), zero.res.fills.size());
    for (std::size_t i = 0; i < back.res.fills.size() && i < zero.res.fills.size(); ++i) {
        CHECK_EQ(back.res.fills[i].ts_ns, zero.res.fills[i].ts_ns);
        CHECK_EQ(back.res.fills[i].qty_lots, zero.res.fills[i].qty_lots);
        CHECK_EQ(back.res.fills[i].queue_ahead_before, zero.res.fills[i].queue_ahead_before);
    }
    // f = 1 is NOT optimistic: it still needs trades once the queue is gone.
    const sc::RunOut one = run_s1(QueueModel::FifoCancelAhead, 1000000);
    CHECK(filled_by(one, ms(4)) < 200);
    // cumulative fills at t5: OPT 200 >= CA(.5) 150 >= CA(.25) 100 >= BACK 50
    const std::int64_t opt = filled_by(run_s1(QueueModel::Optimistic, 0), ms(5));
    const std::int64_t c5 = filled_by(run_s1(QueueModel::FifoCancelAhead, 500000), ms(5));
    const std::int64_t c25 = filled_by(run_s1(QueueModel::FifoCancelAhead, 250000), ms(5));
    const std::int64_t bk = filled_by(back, ms(5));
    CHECK_EQ(opt, 200);
    CHECK_EQ(c5, 150);
    CHECK_EQ(c25, 100);
    CHECK_EQ(bk, 50);
    CHECK(opt >= c5 && c5 >= c25 && c25 >= bk);
}

// ---------------------------------------------------------------------------------------------
// S2: partial fills and level re-add (FIFO_BACK)
// ---------------------------------------------------------------------------------------------

TEST(s2_partial_fills_and_level_readd) {
    fb::Builder b;
    b.snapshot(ms(0), {{100, 100}, {99, 300}}, {{101, 400}});
    b.sell(ms(3), 100, 60);
    b.set(ms(3) + us(100), Side::Bid, 100, 40);
    b.del(ms(4), Side::Bid, 100);
    b.set(ms(5), Side::Bid, 100, 300);   // re-add: the new size is behind us
    b.sell(ms(6), 100, 120);
    b.set(ms(6) + us(100), Side::Bid, 100, 180);
    b.sell(ms(7), 100, 500);
    b.del(ms(7) + us(100), Side::Bid, 100);
    sc::ScriptedStrategy s({sc::send(ms(1), Side::Bid, 100, 200)});
    const sc::RunOut r = sc::run(b.ev, s, sc::make_cfg(QueueModel::FifoBack));
    expect_fills(r, {{ms(6), 120, FillKind::TradeAt}, {ms(7), 80, FillKind::TradeAt}});
    CHECK_EQ(r.res.fills[0].queue_ahead_before, 0);
    CHECK_EQ(r.res.orders[0].queue_ahead_at_arrival, 100);
    CHECK_EQ(r.res.orders[0].filled_lots, 200);
    CHECK(r.res.orders[0].status == bt::OrderStatus::Filled);
    const auto fills = s.reports_of(ReportType::Filled);
    CHECK_EQ(fills.size(), static_cast<std::size_t>(2));
    if (fills.size() == 2) {
        CHECK_EQ(fills[0].qty_lots, 120);
        CHECK_EQ(fills[0].remaining_lots, 80);
        CHECK_EQ(fills[1].qty_lots, 80);
        CHECK_EQ(fills[1].remaining_lots, 0);
    }
    CHECK_EQ(r.res.summary.orders_with_fill, 1);
    CHECK_EQ(r.res.summary.final_position_lots, 200);
    // the 500-lot print exceeded the 180 shown: 320 lots of credit cannot be explained by the delete
    CHECK_EQ(r.res.summary.trade_credit_unmatched_lots, 320);
    // fills.csv carries both rows
    CHECK(r.fills_csv.find(",1,1.00,0.120,") != std::string::npos);
    CHECK(r.fills_csv.find(",1,1.00,0.080,") != std::string::npos);
}

// ---------------------------------------------------------------------------------------------
// S3: cancel in flight
// ---------------------------------------------------------------------------------------------

namespace {
struct S3Out {
    sc::RunOut out;
    std::vector<bt::Report> reports;
};
S3Out run_s3(QueueModel m, std::int64_t trade_ts, std::int64_t trade_qty) {
    fb::Builder b;
    b.snapshot(ms(0), {{100, 500}}, {{101, 500}});
    b.sell(trade_ts, 100, trade_qty);
    b.set(ms(30), Side::Ask, 101, 500);  // keeps the market clock running past the last report
    sc::ScriptedStrategy s({sc::send(ms(1), Side::Bid, 100, 1000), sc::cancel(ms(10), 0)});
    S3Out o;
    o.out = sc::run(b.ev, s, sc::make_cfg(m, 0, ms(5)));
    o.reports = s.reports;
    return o;
}
}  // namespace

TEST(s3a_trade_between_cancel_decision_and_arrival_fills) {
    const S3Out o = run_s3(QueueModel::Optimistic, ms(12), 300);
    CHECK(o.out.ok);
    CHECK_EQ(o.reports.size(), static_cast<std::size_t>(3));
    if (o.reports.size() == 3) {
        expect_report(o.reports[0], ReportType::Accepted, ms(11), 0, 1000, ms(6));
        expect_report(o.reports[1], ReportType::Filled, ms(17), 300, 700, ms(12));
        expect_report(o.reports[2], ReportType::Canceled, ms(20), 700, 0, ms(15));
    }
    CHECK_EQ(o.out.res.fills.size(), static_cast<std::size_t>(1));
    CHECK_EQ(o.out.res.fills[0].ts_ns, ms(12));
    CHECK(o.out.res.fills[0].cancel_in_flight);
    const auto& rec = o.out.res.orders[0];
    CHECK_EQ(rec.send_ns, ms(1));
    CHECK_EQ(rec.arrive_ns, ms(6));
    CHECK_EQ(rec.cancel_send_ns, ms(10));
    CHECK_EQ(rec.cancel_arrive_ns, ms(15));
    CHECK_EQ(rec.filled_in_cancel_flight_lots, 300);
    CHECK(rec.status == bt::OrderStatus::Canceled);
    CHECK_EQ(o.out.res.summary.cancels_in_flight_filled, 1);
    CHECK_EQ(o.out.res.summary.cancels_effective, 1);
    CHECK_EQ(o.out.res.summary.cancels_sent, 1);
}

TEST(s3b_trade_after_cancel_took_effect_does_not_fill) {
    const S3Out o = run_s3(QueueModel::Optimistic, ms(16), 300);
    CHECK_EQ(o.out.res.fills.size(), static_cast<std::size_t>(0));
    CHECK_EQ(o.reports.size(), static_cast<std::size_t>(2));
    if (o.reports.size() == 2) {
        expect_report(o.reports[0], ReportType::Accepted, ms(11), 0, 1000, ms(6));
        expect_report(o.reports[1], ReportType::Canceled, ms(20), 1000, 0, ms(15));
    }
    CHECK_EQ(o.out.res.summary.cancels_in_flight_filled, 0);
    CHECK_EQ(o.out.res.summary.cancels_effective, 1);
}

TEST(s3c_trade_at_exactly_cancel_arrival_still_fills) {
    const S3Out o = run_s3(QueueModel::Optimistic, ms(15), 300);
    CHECK_EQ(o.out.res.fills.size(), static_cast<std::size_t>(1));
    CHECK(o.out.res.fills[0].cancel_in_flight);
    CHECK_EQ(o.reports.size(), static_cast<std::size_t>(3));
    if (o.reports.size() == 3) {
        expect_report(o.reports[1], ReportType::Filled, ms(20), 300, 700, ms(15));
        expect_report(o.reports[2], ReportType::Canceled, ms(20), 700, 0, ms(15));
    }
    CHECK_EQ(o.out.res.summary.cancels_in_flight_filled, 1);
}

TEST(s3d_full_fill_while_cancel_in_flight_makes_cancel_too_late) {
    const S3Out o = run_s3(QueueModel::Optimistic, ms(12), 1500);
    CHECK_EQ(o.out.res.fills.size(), static_cast<std::size_t>(1));
    CHECK_EQ(o.out.res.fills[0].qty_lots, 1000);
    CHECK_EQ(o.reports.size(), static_cast<std::size_t>(3));
    if (o.reports.size() == 3) {
        expect_report(o.reports[1], ReportType::Filled, ms(17), 1000, 0, ms(12));
        CHECK(o.reports[2].type == ReportType::CancelRejected);
        CHECK(o.reports[2].reason == bt::RejectReason::TooLateToCancel);
        CHECK_EQ(o.reports[2].ts_ns, ms(20));
    }
    CHECK_EQ(o.out.res.summary.cancels_too_late, 1);
    CHECK_EQ(o.out.res.summary.cancels_effective, 0);
    CHECK_EQ(o.out.res.summary.cancels_in_flight_filled, 1);
    CHECK(o.out.res.orders[0].status == bt::OrderStatus::Filled);
}

TEST(s3e_fifo_back_queue_protects_the_order) {
    const S3Out o = run_s3(QueueModel::FifoBack, ms(12), 300);
    CHECK_EQ(o.out.res.fills.size(), static_cast<std::size_t>(0));
    CHECK_EQ(o.reports.size(), static_cast<std::size_t>(2));
    if (o.reports.size() == 2) expect_report(o.reports[1], ReportType::Canceled, ms(20), 1000, 0, ms(15));
    CHECK_EQ(o.out.res.summary.cancels_in_flight_filled, 0);
}

TEST(s3_partial_fill_in_flight_counts_once_per_cancel) {
    // two cancels for one order: the second finds it canceled (UnknownOrder); in-flight counted once
    fb::Builder b;
    b.snapshot(ms(0), {{100, 500}}, {{101, 500}});
    b.sell(ms(12), 100, 300);
    b.set(ms(30), Side::Ask, 101, 500);
    sc::ScriptedStrategy s({sc::send(ms(1), Side::Bid, 100, 1000), sc::cancel(ms(10), 0), sc::cancel(ms(11), 0)});
    const sc::RunOut r = sc::run(b.ev, s, sc::make_cfg(QueueModel::Optimistic, 0, ms(5)));
    CHECK_EQ(r.res.summary.cancels_sent, 2);
    CHECK_EQ(r.res.summary.cancels_effective, 1);
    CHECK_EQ(r.res.summary.cancels_unknown, 1);
    CHECK_EQ(r.res.summary.cancels_in_flight_filled, 1);
    const auto rej = s.reports_of(ReportType::CancelRejected);
    CHECK_EQ(rej.size(), static_cast<std::size_t>(1));
    if (rej.size() == 1) CHECK(rej[0].reason == bt::RejectReason::UnknownOrder);
}

// ---------------------------------------------------------------------------------------------
// S4: latency ordering and the shared FIFO channel
// ---------------------------------------------------------------------------------------------

TEST(s4a_zero_latency_trade_at_same_ts_is_processed_before_the_order) {
    fb::Builder b;
    b.snapshot(ms(0), {{100, 500}}, {{101, 500}});
    b.sell(ms(1), 100, 100);
    b.set(ms(5), Side::Ask, 101, 500);
    sc::ScriptedStrategy s({sc::send(ms(1), Side::Bid, 100, 200)});
    const sc::RunOut r = sc::run(b.ev, s, sc::make_cfg(QueueModel::Optimistic));
    CHECK_EQ(r.res.fills.size(), static_cast<std::size_t>(0));
    CHECK_EQ(r.res.orders[0].arrive_ns, ms(1));
    CHECK(r.res.orders[0].status == bt::OrderStatus::Open);  // live at the end
    CHECK_EQ(r.res.summary.orders_open_at_end, 1);
    CHECK_EQ(s.reports.size(), static_cast<std::size_t>(1));
    if (!s.reports.empty()) CHECK_EQ(s.reports[0].ts_ns, ms(1));
}

TEST(s4b_entry_latency_decides_whether_the_trade_sees_the_order) {
    auto run_with_trade_at = [](std::int64_t trade_ts) {
        fb::Builder b;
        b.snapshot(ms(0), {{100, 500}}, {{101, 500}});
        b.sell(trade_ts, 100, 100);
        b.set(ms(20), Side::Ask, 101, 500);
        sc::ScriptedStrategy s({sc::send(ms(0), Side::Bid, 100, 200)});
        bt::BacktestConfig c = sc::make_cfg(QueueModel::Optimistic);
        c.latency.entry_ns = ms(5);
        return sc::run(b.ev, s, c);
    };
    const sc::RunOut at = run_with_trade_at(ms(5));
    CHECK_EQ(at.res.fills.size(), static_cast<std::size_t>(0));
    CHECK_EQ(at.res.orders[0].arrive_ns, ms(5));
    const sc::RunOut after = run_with_trade_at(ms(5) + 1);
    CHECK_EQ(after.res.fills.size(), static_cast<std::size_t>(1));
    if (!after.res.fills.empty()) CHECK_EQ(after.res.fills[0].ts_ns, ms(5) + 1);
}

TEST(s4c_report_timing_and_exchange_order_at_equal_ts) {
    fb::Builder b;
    b.snapshot(ms(0), {{100, 500}}, {{101, 500}});
    b.sell(ms(20), 100, 300);
    b.set(ms(30), Side::Ask, 101, 500);
    sc::ScriptedStrategy s({sc::send(ms(1), Side::Bid, 100, 200), sc::send(ms(1), Side::Bid, 100, 200)});
    bt::BacktestConfig c = sc::make_cfg(QueueModel::Optimistic);
    c.latency.ack_ns = ms(3);
    const sc::RunOut r = sc::run(b.ev, s, c);
    const auto fills = s.reports_of(ReportType::Filled);
    CHECK_EQ(fills.size(), static_cast<std::size_t>(2));
    if (fills.size() == 2) {
        CHECK_EQ(fills[0].exch_ts_ns, ms(20));
        CHECK_EQ(fills[0].ts_ns, ms(23));
        CHECK_EQ(fills[0].id, 1);
        CHECK_EQ(fills[0].qty_lots, 200);
        CHECK_EQ(fills[1].ts_ns, ms(23));
        CHECK_EQ(fills[1].id, 2);
        CHECK_EQ(fills[1].qty_lots, 100);
    }
    // accepted reports: order arrives at 1 ms, delivered at 4 ms
    const auto acc = s.reports_of(ReportType::Accepted);
    CHECK_EQ(acc.size(), static_cast<std::size_t>(2));
    if (acc.size() == 2) CHECK_EQ(acc[0].ts_ns, ms(4));
    CHECK(r.ok);
}

TEST(s4d_cancel_never_overtakes_its_order_on_the_shared_channel) {
    fb::Builder b;
    b.snapshot(ms(0), {{100, 500}}, {{101, 500}});
    b.set(ms(30), Side::Ask, 101, 500);
    sc::ScriptedStrategy s({sc::send(ms(10), Side::Bid, 100, 200), sc::cancel(ms(11), 0)});
    bt::BacktestConfig c = sc::make_cfg(QueueModel::FifoBack);
    c.latency.entry_ns = ms(5);
    c.latency.cancel_ns = ms(1);  // faster than entry, still cannot overtake
    const sc::RunOut r = sc::run(b.ev, s, c);
    CHECK_EQ(s.reports.size(), static_cast<std::size_t>(2));
    if (s.reports.size() == 2) {
        CHECK(s.reports[0].type == ReportType::Accepted);
        CHECK(s.reports[1].type == ReportType::Canceled);
        CHECK_EQ(s.reports[0].exch_ts_ns, ms(15));
        CHECK_EQ(s.reports[1].exch_ts_ns, ms(15));
        CHECK_EQ(s.reports[1].qty_lots, 200);
    }
    CHECK_EQ(r.res.summary.cancels_unknown, 0);
    CHECK_EQ(r.res.summary.cancels_effective, 1);
    CHECK_EQ(r.res.orders[0].cancel_arrive_ns, ms(15));
}

TEST(s4e_zero_latency_never_has_a_fill_in_cancel_flight) {
    // S3-style traffic at L = 0: the trade at exactly the cancel's decision time is not "in flight"
    fb::Builder b;
    b.snapshot(ms(0), {{100, 500}}, {{101, 500}});
    b.sell(ms(10), 100, 300);
    b.sell(ms(12), 100, 300);
    b.set(ms(30), Side::Ask, 101, 500);
    sc::ScriptedStrategy s({sc::send(ms(1), Side::Bid, 100, 1000), sc::cancel(ms(10), 0)});
    const sc::RunOut r = sc::run(b.ev, s, sc::make_cfg(QueueModel::Optimistic));
    CHECK_EQ(r.res.fills.size(), static_cast<std::size_t>(1));
    if (!r.res.fills.empty()) {
        CHECK_EQ(r.res.fills[0].ts_ns, ms(10));
        CHECK(!r.res.fills[0].cancel_in_flight);
    }
    CHECK_EQ(r.res.summary.cancels_in_flight_filled, 0);
    CHECK_EQ(r.res.summary.cancels_effective, 1);
    CHECK_EQ(s.reports.back().qty_lots, 700);
    CHECK_EQ(s.reports.back().ts_ns, ms(10));
}

// ---------------------------------------------------------------------------------------------
// S5: crosses and post-only
// ---------------------------------------------------------------------------------------------

namespace {
std::vector<md::Event> s5_feed() {
    fb::Builder b;
    b.snapshot(ms(0), {{100, 500}}, {{101, 400}});
    b.depth(ms(2), {{Side::Bid, 101, 300}, {Side::Ask, 101, 0}, {Side::Ask, 102, 500}});
    b.depth(ms(4), {{Side::Bid, 102, 300}, {Side::Ask, 102, 0}, {Side::Ask, 103, 400}});
    b.set(ms(5), Side::Bid, 102, 350);
    b.del(ms(6), Side::Bid, 102);
    b.set(ms(7), Side::Bid, 102, 100);
    b.set(ms(9), Side::Ask, 104, 100);
    return b.ev;
}
sc::RunOut run_s5(QueueModel m, std::int64_t ppm, bool cross) {
    sc::ScriptedStrategy s({sc::send(ms(1), Side::Ask, 101, 200), sc::send(ms(3), Side::Ask, 102, 500)});
    return sc::run(s5_feed(), s, sc::make_cfg(m, ppm, 0, cross));
}
}  // namespace

TEST(s5_market_moving_onto_a_resting_order_fills_once_at_our_price) {
    for (const auto& mp : std::vector<std::pair<QueueModel, std::int64_t>>{
             {QueueModel::Optimistic, 0}, {QueueModel::FifoBack, 0}, {QueueModel::FifoCancelAhead, 500000}}) {
        const sc::RunOut r = run_s5(mp.first, mp.second, true);
        CHECK(r.ok);
        CHECK_EQ(r.res.fills.size(), static_cast<std::size_t>(3));
        if (r.res.fills.size() != 3) continue;
        // first order: 200 of the 300 now bid at 101, at OUR price 101
        CHECK_EQ(r.res.fills[0].ts_ns, ms(2));
        CHECK_EQ(r.res.fills[0].qty_lots, 200);
        CHECK_EQ(r.res.fills[0].px_ticks, 101);
        CHECK(r.res.fills[0].kind == FillKind::Cross);
        CHECK(r.res.fills[0].side == Side::Ask);
        CHECK_EQ(r.res.fills[0].queue_ahead_before, 0);  // the level was deleted in the same message: q = 0
        // second order: partial 300 of 500, then no second fill from the same liquidity, then 100 on re-cross
        CHECK_EQ(r.res.fills[1].ts_ns, ms(4));
        CHECK_EQ(r.res.fills[1].qty_lots, 300);
        CHECK_EQ(r.res.fills[1].px_ticks, 102);
        CHECK_EQ(r.res.fills[2].ts_ns, ms(7));
        CHECK_EQ(r.res.fills[2].qty_lots, 100);
        CHECK(r.res.fills[2].kind == FillKind::Cross);
        CHECK_EQ(r.res.summary.fills_cross, 3);
        CHECK_EQ(r.res.orders[1].filled_lots, 500 - 100);
        CHECK(r.res.orders[1].status == bt::OrderStatus::Open);
    }
}

TEST(s5_cross_fills_off_is_an_ablation) {
    const sc::RunOut r = run_s5(QueueModel::Optimistic, 0, false);
    CHECK(r.ok);
    CHECK_EQ(r.res.fills.size(), static_cast<std::size_t>(0));
    CHECK_EQ(r.res.summary.fills_cross, 0);
    CHECK(!r.res.summary.cross_fills);
}

TEST(s5_post_only_rejection_would_cross) {
    fb::Builder b;
    b.snapshot(ms(0), {{100, 500}}, {{101, 400}});
    b.set(ms(10), Side::Ask, 101, 450);
    sc::ScriptedStrategy s({sc::send(ms(1), Side::Bid, 101, 100),    // = best ask: would lock
                            sc::send(ms(2), Side::Bid, 102, 100),    // through the ask
                            sc::send(ms(3), Side::Ask, 100, 100),    // = best bid
                            sc::send(ms(4), Side::Ask, 101, 100),    // joins the ask: fine
                            sc::send(ms(5), Side::Bid, 99, 100)});   // behind the touch: fine
    const sc::RunOut r = sc::run(b.ev, s, sc::make_cfg(QueueModel::FifoBack));
    const auto rej = s.reports_of(ReportType::Rejected);
    CHECK_EQ(rej.size(), static_cast<std::size_t>(3));
    for (const bt::Report& x : rej) CHECK(x.reason == bt::RejectReason::WouldCross);
    CHECK_EQ(r.res.summary.orders_rejected_would_cross, 3);
    CHECK_EQ(r.res.summary.orders_accepted, 2);
    CHECK(r.res.orders[0].status == bt::OrderStatus::Rejected);
    CHECK(r.res.orders[0].reject == bt::RejectReason::WouldCross);
    CHECK_EQ(r.res.orders[0].queue_ahead_at_arrival, -1);
    CHECK(r.res.orders[3].status == bt::OrderStatus::Open);
    CHECK_EQ(r.res.orders[3].queue_ahead_at_arrival, 400);
    CHECK_EQ(r.res.orders[4].queue_ahead_at_arrival, 0);  // level absent at arrival: D = 0, q = 0
}

TEST(s5_order_arriving_before_the_first_snapshot_is_rejected_book_not_live) {
    fb::Builder b;
    b.set(ms(0), Side::Bid, 100, 500);  // an update with no snapshot: session stays INIT
    b.snapshot(ms(3), {{100, 500}}, {{101, 400}});
    b.set(ms(8), Side::Ask, 101, 450);
    sc::ScriptedStrategy s({sc::send(ms(1), Side::Bid, 100, 100), sc::send(ms(5), Side::Bid, 100, 100)});
    const sc::RunOut r = sc::run(b.ev, s, sc::make_cfg(QueueModel::FifoBack));
    CHECK(r.ok);
    CHECK_EQ(s.reports.size(), static_cast<std::size_t>(2));
    if (s.reports.size() == 2) {
        CHECK(s.reports[0].type == ReportType::Rejected);
        CHECK(s.reports[0].reason == bt::RejectReason::BookNotLive);
        CHECK(s.reports[1].type == ReportType::Accepted);
    }
    CHECK_EQ(r.res.summary.orders_rejected_not_live, 1);
    CHECK(r.res.orders[0].reject == bt::RejectReason::BookNotLive);
    CHECK_EQ(s.live_log.front().second, false);
    CHECK_EQ(s.live_log.back().second, true);
}

// ---------------------------------------------------------------------------------------------
// S6: several of our orders at one price
// ---------------------------------------------------------------------------------------------

TEST(s6_volume_is_shared_in_priority_order_optimistic) {
    fb::Builder b;
    b.snapshot(ms(0), {{100, 500}}, {{101, 400}});
    b.sell(ms(3), 100, 300);
    b.set(ms(4), Side::Ask, 101, 450);
    sc::ScriptedStrategy s({sc::send(ms(1), Side::Bid, 100, 200), sc::send(ms(2), Side::Bid, 100, 200)});
    const sc::RunOut r = sc::run(b.ev, s, sc::make_cfg(QueueModel::Optimistic));
    expect_fills(r, {{ms(3), 200, FillKind::TradeAt}, {ms(3), 100, FillKind::TradeAt}});
    CHECK_EQ(r.res.fills[0].id, 1);
    CHECK_EQ(r.res.fills[1].id, 2);
    CHECK_EQ(r.res.orders[1].filled_lots, 100);
}

TEST(s6_fifo_back_each_order_sees_the_remaining_volume) {
    fb::Builder b;
    b.snapshot(ms(0), {{100, 100}}, {{101, 400}});
    b.sell(ms(3), 100, 400);
    b.set(ms(4), Side::Ask, 101, 450);
    sc::ScriptedStrategy s({sc::send(ms(1), Side::Bid, 100, 200), sc::send(ms(2), Side::Bid, 100, 200)});
    const sc::RunOut r = sc::run(b.ev, s, sc::make_cfg(QueueModel::FifoBack));
    expect_fills(r, {{ms(3), 200, FillKind::TradeAt}, {ms(3), 100, FillKind::TradeAt}});
    CHECK_EQ(r.res.orders[0].queue_ahead_at_arrival, 100);
    CHECK_EQ(r.res.orders[1].queue_ahead_at_arrival, 100);
    CHECK_EQ(r.res.fills[0].queue_ahead_before, 100);
}

TEST(trade_price_rules_for_asks_and_untouched_worse_orders) {
    fb::Builder b;
    b.snapshot(ms(0), {{100, 500}, {99, 500}}, {{101, 400}, {102, 400}});
    b.buy(ms(5), 102, 300);            // buyer lifted up to 102: our ask at 101 is THROUGH, ask at 103 untouched
    b.set(ms(6), Side::Ask, 101, 450);
    b.sell(ms(7), 100, 50);            // our bid at 99 is worse than the print: untouched
    b.set(ms(8), Side::Bid, 100, 450);
    sc::ScriptedStrategy s({sc::send(ms(1), Side::Ask, 101, 100), sc::send(ms(1), Side::Ask, 103, 100),
                            sc::send(ms(2), Side::Bid, 99, 100)});
    const sc::RunOut r = sc::run(b.ev, s, sc::make_cfg(QueueModel::FifoBack));
    CHECK_EQ(r.res.fills.size(), static_cast<std::size_t>(1));
    if (!r.res.fills.empty()) {
        CHECK_EQ(r.res.fills[0].id, 1);
        CHECK(r.res.fills[0].kind == FillKind::TradeThrough);
        CHECK_EQ(r.res.fills[0].px_ticks, 101);
        CHECK(r.res.fills[0].side == Side::Ask);
    }
    CHECK_EQ(r.res.summary.final_position_lots, -100);
}

// ---------------------------------------------------------------------------------------------
// S8: stale session
// ---------------------------------------------------------------------------------------------

TEST(s8_sequence_gap_session_goes_stale_and_arrivals_are_rejected) {
    fb::Builder b;
    b.snapshot(ms(0), {{100, 500}}, {{101, 400}});
    b.set(ms(2), Side::Bid, 100, 600);
    b.skip_seq(5);
    b.set(ms(3), Side::Bid, 100, 650);  // gap: session STALE
    b.set(ms(7), Side::Ask, 101, 410);  // buffered
    b.snapshot(ms(9), {{100, 650}}, {{101, 410}});  // resync: LIVE again
    b.set(ms(12), Side::Ask, 101, 420);
    sc::ScriptedStrategy s({sc::send(ms(1), Side::Bid, 100, 200),   // accepted
                            sc::cancel(ms(4), 0),                   // strategy reacts to live=false
                            sc::send(ms(5), Side::Bid, 100, 200),   // arrives while STALE
                            sc::send(ms(10), Side::Bid, 100, 200)});  // after the resync
    const sc::RunOut r = sc::run(b.ev, s, sc::make_cfg(QueueModel::FifoBack));
    CHECK(r.ok);
    bool saw_stale = false, saw_live_again = false;
    for (const auto& p : s.live_log) {
        if (p.first >= ms(3) && p.first < ms(9) && !p.second) saw_stale = true;
        if (p.first >= ms(9) && p.second) saw_live_again = true;
    }
    CHECK(saw_stale);
    CHECK(saw_live_again);
    CHECK_EQ(r.res.summary.session_gaps, 1);
    CHECK_EQ(r.res.summary.session_resyncs, 1);
    CHECK(r.res.summary.session_final_state == "live");
    CHECK_EQ(r.res.summary.orders_rejected_not_live, 1);
    CHECK(r.res.orders[1].reject == bt::RejectReason::BookNotLive);
    CHECK(r.res.orders[0].status == bt::OrderStatus::Canceled);
    CHECK(r.res.orders[2].status == bt::OrderStatus::Open);
    CHECK_EQ(r.res.orders[2].queue_ahead_at_arrival, 650);
}

TEST(s8_queue_is_only_clamped_while_the_session_is_not_live) {
    // q = 500 < D = 900. A crossing message (session goes STALE) shrinks the level to 800. The
    // live-path rule with f = 0.5 would cut 50 from the queue ahead (450); while not live q is only
    // clamped to the displayed size (500). A later print of 600 then fills 100 lots, not 150.
    fb::Builder b;
    b.snapshot(ms(0), {{100, 500}}, {{101, 400}});
    b.set(ms(2), Side::Bid, 100, 900);
    b.depth(ms(3), {{Side::Bid, 100, 800}, {Side::Bid, 102, 100}});  // crosses the ask at 101
    b.snapshot(ms(4), {{100, 800}}, {{101, 400}});                   // resync
    b.sell(ms(5), 100, 600);
    b.set(ms(6), Side::Bid, 100, 200);
    sc::ScriptedStrategy s({sc::send(ms(1), Side::Bid, 100, 200)});
    const sc::RunOut r = sc::run(b.ev, s, sc::make_cfg(QueueModel::FifoCancelAhead, 500000));
    CHECK(r.ok);
    CHECK_EQ(r.res.fills.size(), static_cast<std::size_t>(1));
    if (!r.res.fills.empty()) {
        CHECK_EQ(r.res.fills[0].qty_lots, 100);
        CHECK_EQ(r.res.fills[0].queue_ahead_before, 500);
    }
    // sanity: the same feed with a live path (no crossing) gives the cut queue
    fb::Builder c;
    c.snapshot(ms(0), {{100, 500}}, {{101, 400}});
    c.set(ms(2), Side::Bid, 100, 900);
    c.set(ms(3), Side::Bid, 100, 800);
    c.sell(ms(5), 100, 600);
    c.set(ms(6), Side::Bid, 100, 200);
    sc::ScriptedStrategy s2({sc::send(ms(1), Side::Bid, 100, 200)});
    const sc::RunOut r2 = sc::run(c.ev, s2, sc::make_cfg(QueueModel::FifoCancelAhead, 500000));
    CHECK_EQ(r2.res.fills.size(), static_cast<std::size_t>(1));
    if (!r2.res.fills.empty()) {
        CHECK_EQ(r2.res.fills[0].qty_lots, 150);
        CHECK_EQ(r2.res.fills[0].queue_ahead_before, 450);
    }
}
