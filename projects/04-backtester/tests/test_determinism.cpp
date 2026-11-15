// Section 9.3: determinism and the model-ordering property, on an in-code random feed.
#include <algorithm>
#include <cstdio>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "bt/output.hpp"
#include "bt/strategy_mm.hpp"
#include "bt/strategy_schedule.hpp"
#include "feed_builder.hpp"
#include "harness.hpp"
#include "scripted.hpp"

using bt::QueueModel;
using fb::ms;
using fb::us;

namespace {

struct Full {
    sc::RunOut out;
    std::string orders_csv, summary_csv;
};

Full run_mm(const std::vector<md::Event>& ev, const bt::BacktestConfig& cfg) {
    bt::MarketMaker mm;
    Full f;
    f.out = sc::run(ev, mm, cfg);
    f.out.res.summary.strategy = "mm";
    std::ostringstream o, s;
    bt::write_orders_csv(o, f.out.res.orders);
    f.orders_csv = o.str();
    f.out.res.summary.fills = static_cast<std::int64_t>(f.out.res.fills.size());
    f.out.res.summary.fills_fnv1a64 = bt::fnv1a64(f.out.fills_csv);
    bt::write_summary_csv(s, f.out.res.summary);
    f.summary_csv = s.str();
    return f;
}

std::map<bt::OrderId, std::int64_t> filled_by_order(const bt::BacktestResult& r) {
    std::map<bt::OrderId, std::int64_t> m;
    for (const bt::OrderRecord& o : r.orders) m[o.id] = o.filled_lots;
    return m;
}

sc::RunOut run_schedule(const std::vector<md::Event>& ev, QueueModel m, std::int64_t ppm, std::int64_t latency) {
    bt::ScheduledQuoter q;  // period 500 ms, life 400 ms
    return sc::run(ev, q, sc::make_cfg(m, ppm, latency));
}

}  // namespace

TEST(determinism_two_runs_give_identical_bytes_and_hash) {
    const std::vector<md::Event> ev = fb::random_feed(21, 6000);
    const bt::BacktestConfig cfg = sc::make_cfg(QueueModel::FifoCancelAhead, 500000, ms(5));
    const Full a = run_mm(ev, cfg);
    const Full b = run_mm(ev, cfg);
    CHECK(a.out.ok && b.out.ok);
    CHECK(a.out.res.fills.size() >= 50);
    CHECK(a.out.fills_csv == b.out.fills_csv);
    CHECK_EQ(bt::fnv1a64(a.out.fills_csv), bt::fnv1a64(b.out.fills_csv));
    CHECK(a.orders_csv == b.orders_csv);
    CHECK(a.summary_csv == b.summary_csv);
    CHECK(a.out.market == b.out.market);
    std::printf("      [info] determinism feed: %zu fills, %lld orders\n", a.out.res.fills.size(),
                static_cast<long long>(a.out.res.summary.orders_sent));
}

TEST(determinism_jitter_zero_ignores_the_seed) {
    const std::vector<md::Event> ev = fb::random_feed(22, 6000);
    bt::BacktestConfig c1 = sc::make_cfg(QueueModel::FifoBack, 0, ms(3));
    bt::BacktestConfig c2 = c1;
    c1.latency.seed = 1;
    c2.latency.seed = 2;
    const Full a = run_mm(ev, c1), b = run_mm(ev, c2);
    CHECK(a.out.fills_csv == b.out.fills_csv);
    CHECK_EQ(bt::fnv1a64(a.out.fills_csv), bt::fnv1a64(b.out.fills_csv));
}

TEST(determinism_jitter_depends_on_the_seed_only) {
    const std::vector<md::Event> ev = fb::random_feed(23, 6000);
    bt::BacktestConfig c1 = sc::make_cfg(QueueModel::FifoCancelAhead, 500000, ms(1));
    c1.latency.jitter_mean_ns = us(200);
    bt::BacktestConfig c2 = c1;
    c1.latency.seed = 1;
    c2.latency.seed = 2;
    const Full a = run_mm(ev, c1), a_again = run_mm(ev, c1), b = run_mm(ev, c2);
    CHECK(a.out.res.fills.size() >= 50);  // precondition for the hash comparison to mean anything
    CHECK(a.out.fills_csv == a_again.out.fills_csv);
    CHECK_EQ(bt::fnv1a64(a.out.fills_csv), bt::fnv1a64(a_again.out.fills_csv));
    CHECK(a.orders_csv == a_again.orders_csv);
    CHECK(bt::fnv1a64(a.out.fills_csv) != bt::fnv1a64(b.out.fills_csv));
}

TEST(ordering_property_with_a_fill_independent_schedule) {
    const std::vector<md::Event> ev = fb::random_feed(31, 20000);
    for (std::int64_t latency : {std::int64_t{0}, ms(5)}) {
        const sc::RunOut opt = run_schedule(ev, QueueModel::Optimistic, 0, latency);
        const sc::RunOut c50 = run_schedule(ev, QueueModel::FifoCancelAhead, 500000, latency);
        const sc::RunOut c25 = run_schedule(ev, QueueModel::FifoCancelAhead, 250000, latency);
        const sc::RunOut back = run_schedule(ev, QueueModel::FifoBack, 0, latency);
        const sc::RunOut c0 = run_schedule(ev, QueueModel::FifoCancelAhead, 0, latency);
        CHECK(opt.ok && c50.ok && c25.ok && back.ok && c0.ok);
        const auto mo = filled_by_order(opt.res), m50 = filled_by_order(c50.res), m25 = filled_by_order(c25.res),
                   mb = filled_by_order(back.res);
        CHECK(mo.size() > 100);
        // identical order id sets (the schedule does not depend on fills)
        bool same_ids = mo.size() == m50.size() && mo.size() == m25.size() && mo.size() == mb.size();
        for (const auto& kv : mo) same_ids = same_ids && m50.count(kv.first) == 1 && m25.count(kv.first) == 1 &&
                                             mb.count(kv.first) == 1;
        CHECK(same_ids);
        int violations = 0;
        std::int64_t t_opt = 0, t_50 = 0, t_25 = 0, t_b = 0;
        for (const auto& kv : mo) {
            const std::int64_t o = kv.second, a = m50.at(kv.first), b = m25.at(kv.first), c = mb.at(kv.first);
            if (!(o >= a && a >= b && b >= c)) ++violations;
            t_opt += o;
            t_50 += a;
            t_25 += b;
            t_b += c;
        }
        CHECK_EQ(violations, 0);
        CHECK(t_opt >= t_50 && t_50 >= t_25 && t_25 >= t_b);
        CHECK(t_opt > t_b);  // the models actually differ on this feed
        // f = 0 is FIFO_BACK, byte for byte
        CHECK(c0.fills_csv == back.fills_csv);
        std::printf("      [info] L=%lld ms schedule filled lots: OPT %lld  CA.5 %lld  CA.25 %lld  BACK %lld\n",
                    static_cast<long long>(latency / 1'000'000), static_cast<long long>(t_opt),
                    static_cast<long long>(t_50), static_cast<long long>(t_25), static_cast<long long>(t_b));
    }
}

TEST(market_maker_fill_counts_are_reported_not_asserted) {
    const std::vector<md::Event> ev = fb::random_feed(41, 12000);
    for (std::int64_t latency : {std::int64_t{0}, ms(5)}) {
        std::printf("      [info] MM L=%lld ms fills:", static_cast<long long>(latency / 1'000'000));
        for (const auto& mp : std::vector<std::pair<QueueModel, std::int64_t>>{{QueueModel::Optimistic, 0},
                                                                                  {QueueModel::FifoCancelAhead, 500000},
                                                                                  {QueueModel::FifoCancelAhead, 250000},
                                                                                  {QueueModel::FifoBack, 0}}) {
            const Full f = run_mm(ev, sc::make_cfg(mp.first, mp.second, latency));
            CHECK(f.out.ok);
            std::printf(" %zu", f.out.res.fills.size());
        }
        std::printf("\n");
    }
}

TEST(market_maker_cancels_in_flight_appear_only_with_latency) {
    const std::vector<md::Event> ev = fb::random_feed(51, 12000);
    const Full l0 = run_mm(ev, sc::make_cfg(QueueModel::Optimistic, 0, 0));
    CHECK_EQ(l0.out.res.summary.cancels_in_flight_filled, 0);
    const Full l20 = run_mm(ev, sc::make_cfg(QueueModel::Optimistic, 0, ms(20)));
    CHECK(l20.out.res.summary.cancels_in_flight_filled > 0);
    CHECK(l20.out.res.summary.fills_cross > 0);
    for (const bt::Fill& f : l0.out.res.fills) CHECK(!f.cancel_in_flight);
    // accounting: fills flagged in-flight add up to the per-order counters
    std::int64_t flagged = 0, per_order = 0;
    for (const bt::Fill& f : l20.out.res.fills)
        if (f.cancel_in_flight) flagged += f.qty_lots;
    for (const bt::OrderRecord& o : l20.out.res.orders) per_order += o.filled_in_cancel_flight_lots;
    CHECK_EQ(flagged, per_order);
}
