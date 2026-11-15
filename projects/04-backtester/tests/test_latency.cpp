// Section 9.1 items 3 and 4: SplitMix64, latency FIFO channels, jitter, event queue ordering.
#include <string>

#include "bt/event_queue.hpp"
#include "bt/latency.hpp"
#include "feed_builder.hpp"
#include "harness.hpp"

using fb::ms;
using fb::us;

TEST(latency_splitmix_known_outputs) {
    bt::SplitMix64 r(1);
    CHECK_EQ(r.next(), 0x910a2dec89025cc1ULL);
    CHECK_EQ(r.next(), 0xbeeb8da1658eec67ULL);
    CHECK_EQ(r.next(), 0xf893a2eefb32555eULL);
    bt::SplitMix64 a(7), b(7);
    for (int i = 0; i < 100; ++i) {
        const double u = a.uniform01();
        CHECK(u >= 0.0 && u < 1.0);
        CHECK(b.uniform01() == u);
    }
}

TEST(latency_fixed_delays_and_shared_fifo_channel) {
    bt::LatencyConfig c;
    c.entry_ns = ms(5);
    c.cancel_ns = ms(1);
    c.ack_ns = ms(3);
    bt::LatencyModel m(c);
    // order sent at 10 arrives at 15; cancel sent at 11 would arrive at 12 but cannot overtake: 15
    CHECK_EQ(m.entry_arrival(ms(10)), ms(15));
    CHECK_EQ(m.cancel_arrival(ms(11)), ms(15));
    // a later cancel uses its own latency again
    CHECK_EQ(m.cancel_arrival(ms(20)), ms(21));
    // and a later order is not ahead of that cancel
    CHECK_EQ(m.entry_arrival(ms(16)), ms(21));
    CHECK_EQ(m.entry_arrival(ms(30)), ms(35));
    // report channel: monotone and shifted by the ack latency
    CHECK_EQ(m.report_arrival(ms(20)), ms(23));
    CHECK_EQ(m.report_arrival(ms(20)), ms(23));
    CHECK_EQ(m.report_arrival(ms(21)), ms(24));
    CHECK_EQ(m.draws(), 0);
}

TEST(latency_jitter_zero_makes_no_draws_and_seed_is_irrelevant) {
    bt::LatencyConfig c1, c2;
    c1.entry_ns = c2.entry_ns = ms(2);
    c1.seed = 1;
    c2.seed = 2;
    bt::LatencyModel a(c1), b(c2);
    for (int i = 0; i < 50; ++i) CHECK_EQ(a.entry_arrival(ms(i * 10)), b.entry_arrival(ms(i * 10)));
    CHECK_EQ(a.draws(), 0);
    CHECK_EQ(b.draws(), 0);
}

TEST(latency_jitter_is_seeded_nonnegative_and_fifo) {
    bt::LatencyConfig c;
    c.entry_ns = us(100);
    c.cancel_ns = us(100);
    c.ack_ns = us(100);
    c.jitter_mean_ns = us(200);
    c.seed = 42;
    bt::LatencyModel a(c), b(c);
    std::int64_t last = -1, sum_extra = 0;
    for (int i = 0; i < 2000; ++i) {
        const std::int64_t send = us(i * 50);
        const std::int64_t t = a.entry_arrival(send);
        CHECK_EQ(t, b.entry_arrival(send));  // same seed, same draws
        CHECK(t >= send + c.entry_ns);       // jitter is non-negative
        CHECK(t >= last);                    // FIFO
        last = t;
        sum_extra += t - send - c.entry_ns;
    }
    CHECK_EQ(a.draws(), 2000);
    // exponential with mean 200 us: the sample mean is in a generous band (FIFO inflates it a little)
    const double mean = static_cast<double>(sum_extra) / 2000.0;
    CHECK(mean > 100'000.0 && mean < 600'000.0);
    bt::LatencyConfig c43 = c;
    c43.seed = 43;
    bt::LatencyModel d(c43), a2(c);
    bool differs = false;
    for (int i = 0; i < 20; ++i)
        if (d.entry_arrival(us(i * 50)) != a2.entry_arrival(us(i * 50))) differs = true;
    CHECK(differs);
}

TEST(event_queue_orders_by_ts_rank_then_push_order) {
    bt::EventQueue q;
    auto mk = [](std::int64_t ts, bt::SimEventKind k, std::int64_t tag) {
        bt::SimEvent e;
        e.ts_ns = ts;
        e.kind = k;
        e.timer_id = tag;
        return e;
    };
    using K = bt::SimEventKind;
    q.push(mk(10, K::Timer, 1));
    q.push(mk(10, K::Report, 2));
    q.push(mk(10, K::ExchangeIn, 3));
    q.push(mk(5, K::Timer, 4));
    q.push(mk(10, K::ExchangeIn, 5));
    q.push(mk(10, K::Report, 6));
    q.push(mk(10, K::Timer, 7));
    CHECK_EQ(q.size(), static_cast<std::size_t>(7));
    std::vector<std::int64_t> order;
    while (!q.empty()) {
        CHECK(q.top().ts_ns >= 0);
        order.push_back(q.pop().timer_id);
    }
    const std::vector<std::int64_t> expect{4, 3, 5, 2, 6, 1, 7};
    CHECK(order == expect);
}

// Review regression: huge latencies or timestamps overflowed signed int64 (undefined behaviour).
TEST(latency_arrival_times_saturate_instead_of_overflowing) {
    constexpr std::int64_t kMax = 9223372036854775807LL;
    bt::LatencyConfig c;
    c.entry_ns = kMax;
    c.cancel_ns = kMax;
    c.ack_ns = kMax;
    bt::LatencyModel m(c);
    CHECK_EQ(m.entry_arrival(kMax), kMax);
    CHECK_EQ(m.cancel_arrival(kMax), kMax);
    CHECK_EQ(m.report_arrival(kMax), kMax);
    CHECK_EQ(m.entry_arrival(5), kMax);
}
