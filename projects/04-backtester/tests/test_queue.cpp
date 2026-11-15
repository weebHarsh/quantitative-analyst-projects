// Section 9.1 item 2: queue models as pure functions, plus parse helpers.
#include "bt/queue.hpp"
#include "bt/latency.hpp"
#include "bt/types.hpp"
#include "harness.hpp"

namespace {
bt::QueueConfig cfg(bt::QueueModel m, std::int64_t ppm = 0) { return bt::QueueConfig{m, ppm}; }
const bt::QueueConfig kOpt = cfg(bt::QueueModel::Optimistic);
const bt::QueueConfig kBack = cfg(bt::QueueModel::FifoBack);
bt::QueueConfig ca(std::int64_t ppm) { return cfg(bt::QueueModel::FifoCancelAhead, ppm); }
}  // namespace

TEST(queue_arrival) {
    CHECK_EQ(bt::queue_on_arrival(kOpt, 700), 0);
    CHECK_EQ(bt::queue_on_arrival(kBack, 700), 700);
    CHECK_EQ(bt::queue_on_arrival(ca(250000), 700), 700);
    CHECK_EQ(bt::queue_on_arrival(kBack, 0), 0);
}

TEST(queue_trade) {
    auto r = bt::queue_on_trade(kBack, 500, 200, 400, false);
    CHECK_EQ(r.fill, 0);
    CHECK_EQ(r.queue_after, 100);
    r = bt::queue_on_trade(kBack, 100, 200, 150, false);
    CHECK_EQ(r.fill, 50);
    CHECK_EQ(r.queue_after, 0);
    r = bt::queue_on_trade(kBack, 500, 200, 300, true);  // through: fills regardless of the queue
    CHECK_EQ(r.fill, 200);
    CHECK_EQ(r.queue_after, 0);
    r = bt::queue_on_trade(kBack, 500, 200, 100, true);
    CHECK_EQ(r.fill, 100);
    r = bt::queue_on_trade(kOpt, 0, 200, 150, false);
    CHECK_EQ(r.fill, 150);
    CHECK_EQ(r.queue_after, 0);
    r = bt::queue_on_trade(ca(500000), 100, 200, 1000, false);
    CHECK_EQ(r.fill, 200);  // capped by remaining
    r = bt::queue_on_trade(kBack, 0, 200, 0, false);
    CHECK_EQ(r.fill, 0);
}

TEST(queue_depth_spec_examples) {
    // (q=500, 800->600, e=0): BACK 500, f=0.25 450, f=0.5 400, f=1 300
    CHECK_EQ(bt::queue_on_depth(kBack, 500, 800, 600, 0), 500);
    CHECK_EQ(bt::queue_on_depth(ca(250000), 500, 800, 600, 0), 450);
    CHECK_EQ(bt::queue_on_depth(ca(500000), 500, 800, 600, 0), 400);
    CHECK_EQ(bt::queue_on_depth(ca(1000000), 500, 800, 600, 0), 300);
    // forced: (q=500, 600->300, e=0, f=0) gives 300
    CHECK_EQ(bt::queue_on_depth(kBack, 500, 600, 300, 0), 300);
    CHECK_EQ(bt::queue_on_depth(ca(0), 500, 600, 300, 0), 300);
    // explained: (q=200, 500->200, e=300) gives 200
    CHECK_EQ(bt::queue_on_depth(kBack, 200, 500, 200, 300), 200);
    // arrived after the print: (q=500, 500->200, e=300) gives 200
    CHECK_EQ(bt::queue_on_depth(kBack, 500, 500, 200, 300), 200);
    // increase: (q=500, 500->900) gives 500
    CHECK_EQ(bt::queue_on_depth(kBack, 500, 500, 900, 0), 500);
    CHECK_EQ(bt::queue_on_depth(ca(500000), 500, 500, 900, 0), 500);
    // delete: (q=300, 300->0) gives 0
    CHECK_EQ(bt::queue_on_depth(kBack, 300, 300, 0, 0), 0);
    CHECK_EQ(bt::queue_on_depth(kBack, 300, 300, 0, 300), 0);
    // optimistic is always 0
    CHECK_EQ(bt::queue_on_depth(kOpt, 0, 800, 600, 0), 0);
    // level absent at arrival (D = 0, q = 0) then added: stays 0
    CHECK_EQ(bt::queue_on_depth(kBack, 0, 0, 400, 0), 0);
    // increase that is smaller than q after a shrink elsewhere: clamped to the new size
    CHECK_EQ(bt::queue_on_depth(kBack, 500, 700, 800, 0), 500);
    CHECK_EQ(bt::queue_on_depth(kBack, 500, 700, 300, 0), 300);  // 400 left, only 300 shown -> clamp
}

TEST(queue_depth_floor_and_no_overflow) {
    // floor(u * ppm / 1e6): u = 7, ppm = 250000 -> 1
    CHECK_EQ(bt::queue_on_depth(ca(250000), 100, 107, 100, 0), 99);
    // u large enough that u * ppm would overflow nothing, but exercise the split arithmetic
    const std::int64_t big = 5'000'000'000'000LL;  // 5e12 lots
    CHECK_EQ(bt::queue_on_depth(ca(500000), big, big + 2'000'000'000'000LL, big, 0), big - 1'000'000'000'000LL);
}

TEST(queue_depth_properties) {
    bt::SplitMix64 rng(12345);
    auto rnd = [&rng](std::int64_t lo, std::int64_t hi) {
        return lo + static_cast<std::int64_t>(rng.next() % static_cast<std::uint64_t>(hi - lo + 1));
    };
    int checked = 0;
    for (int it = 0; it < 10000; ++it) {
        const std::int64_t before = rnd(0, 2000);
        const std::int64_t after = rnd(0, 2000);
        const std::int64_t q = rnd(0, before);  // invariant: 0 <= q <= D
        const std::int64_t expl = rnd(0, 2500);
        const std::int64_t f1 = rnd(0, 1000000), f2 = rnd(0, 1000000);
        const std::int64_t lo = std::min(f1, f2), hi = std::max(f1, f2);
        const std::int64_t a = bt::queue_on_depth(ca(lo), q, before, after, expl);
        const std::int64_t b = bt::queue_on_depth(ca(hi), q, before, after, expl);
        const std::int64_t back = bt::queue_on_depth(kBack, q, before, after, expl);
        const std::int64_t zero = bt::queue_on_depth(ca(0), q, before, after, expl);
        // 0 <= q' <= after, for every model
        if (a < 0 || a > after || b < 0 || b > after || back < 0 || back > after) CHECK(false);
        // non-increasing in f
        if (!(b <= a)) CHECK(false);
        // FIFO_BACK is the largest queue, f = 0 equals it
        if (!(a <= back)) CHECK(false);
        if (zero != back) CHECK(false);
        // non-decreasing in q
        const std::int64_t q2 = rnd(q, before);
        const std::int64_t a2 = bt::queue_on_depth(ca(lo), q2, before, after, expl);
        const std::int64_t back2 = bt::queue_on_depth(kBack, q2, before, after, expl);
        if (!(a2 >= a) || !(back2 >= back)) CHECK(false);
        // never above the old queue
        if (!(back <= q || after < q)) CHECK(false);
        ++checked;
    }
    CHECK_EQ(checked, 10000);
}

TEST(types_parse_helpers) {
    std::int64_t ppm = -1;
    CHECK(bt::parse_fraction_ppm("0", ppm));
    CHECK_EQ(ppm, 0);
    CHECK(bt::parse_fraction_ppm("0.25", ppm));
    CHECK_EQ(ppm, 250000);
    CHECK(bt::parse_fraction_ppm("0.5", ppm));
    CHECK_EQ(ppm, 500000);
    CHECK(bt::parse_fraction_ppm("1", ppm));
    CHECK_EQ(ppm, 1000000);
    CHECK(bt::parse_fraction_ppm("1.000000", ppm));
    CHECK_EQ(ppm, 1000000);
    CHECK(bt::parse_fraction_ppm("0.000001", ppm));
    CHECK_EQ(ppm, 1);
    ppm = 77;
    CHECK(!bt::parse_fraction_ppm("1.000001", ppm));
    CHECK(!bt::parse_fraction_ppm("1.5", ppm));
    CHECK(!bt::parse_fraction_ppm("2", ppm));
    CHECK(!bt::parse_fraction_ppm("0.1234567", ppm));
    CHECK(!bt::parse_fraction_ppm("", ppm));
    CHECK(!bt::parse_fraction_ppm(".5", ppm));
    CHECK(!bt::parse_fraction_ppm("0.", ppm));
    CHECK(!bt::parse_fraction_ppm("-0.5", ppm));
    CHECK(!bt::parse_fraction_ppm("abc", ppm));
    CHECK_EQ(ppm, 77);  // untouched on failure
    bt::QueueModel m = bt::QueueModel::FifoBack;
    CHECK(bt::parse_queue_model("optimistic", m));
    CHECK(m == bt::QueueModel::Optimistic);
    CHECK(bt::parse_queue_model("fifo_cancel_ahead", m));
    CHECK(m == bt::QueueModel::FifoCancelAhead);
    CHECK(!bt::parse_queue_model("fifo", m));
    CHECK(std::string(bt::to_string(bt::FillKind::TradeThrough)) == "TRADE_THROUGH");
    CHECK(std::string(bt::to_string(bt::RejectReason::WouldCross)) == "WOULD_CROSS");
    CHECK(std::string(bt::to_string(bt::OrderStatus::Open)) == "OPEN");
}
