#include "harness.hpp"
#include "md/stats.hpp"

using namespace md;

TEST(stats_nearest_rank) {
    LatencyStats s0;
    const LatencySummary z = s0.summary();
    CHECK_EQ(z.count, 0);
    CHECK_EQ(z.p50_ns, 0);
    CHECK_EQ(z.mean_ns, 0.0);

    LatencyStats s1;
    s1.add(-5);
    const LatencySummary a = s1.summary();
    CHECK_EQ(a.count, 1);
    CHECK_EQ(a.p50_ns, -5);
    CHECK_EQ(a.p99_ns, -5);
    CHECK_EQ(a.min_ns, -5);
    CHECK_EQ(a.max_ns, -5);
    CHECK_EQ(a.mean_ns, -5.0);

    LatencyStats s2;
    s2.add(20);
    s2.add(10);
    const LatencySummary b = s2.summary();  // sorted 10,20: p50 idx (100+99)/100-1 = 0; p99 idx 1
    CHECK_EQ(b.p50_ns, 10);
    CHECK_EQ(b.p99_ns, 20);
    CHECK_EQ(b.sum_ns, 30);
    CHECK_EQ(b.mean_ns, 15.0);

    LatencyStats s100, s101;
    for (int i = 100; i >= 1; --i) s100.add(i);
    for (int i = 1; i <= 101; ++i) s101.add(i);
    const LatencySummary c = s100.summary();  // p50 -> rank 50, p99 -> rank 99
    CHECK_EQ(c.p50_ns, 50);
    CHECK_EQ(c.p99_ns, 99);
    CHECK_EQ(c.min_ns, 1);
    CHECK_EQ(c.max_ns, 100);
    const LatencySummary d = s101.summary();  // p50: (5050+99)/100=51 -> 51; p99: (9999+99)/100=100
    CHECK_EQ(d.p50_ns, 51);
    CHECK_EQ(d.p99_ns, 100);
}
