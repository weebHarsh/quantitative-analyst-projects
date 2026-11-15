#include <map>
#include <random>

#include "harness.hpp"
#include "md/book.hpp"

using namespace md;

TEST(book_insert_replace_remove) {
    Book b;
    CHECK(b.empty(Side::Bid));
    CHECK(b.set_level(Side::Bid, 100, 5) == ApplyResult::Inserted);
    CHECK(b.set_level(Side::Bid, 101, 7) == ApplyResult::Inserted);
    CHECK(b.set_level(Side::Bid, 99, 1) == ApplyResult::Inserted);
    CHECK(b.set_level(Side::Bid, 100, 6) == ApplyResult::Replaced);
    CHECK_EQ(b.depth(Side::Bid), static_cast<std::size_t>(3));
    CHECK_EQ(b.best(Side::Bid).price_ticks, 101);
    CHECK_EQ(b.qty_at(Side::Bid, 100), 6);
    CHECK_EQ(b.qty_at(Side::Bid, 98), 0);
    CHECK(b.remove_level(Side::Bid, 101) == ApplyResult::Removed);
    CHECK(b.remove_level(Side::Bid, 101) == ApplyResult::Missing);
    CHECK_EQ(b.best(Side::Bid).price_ticks, 100);
    CHECK(b.invariants_ok());

    CHECK(b.set_level(Side::Ask, 105, 2) == ApplyResult::Inserted);
    CHECK(b.set_level(Side::Ask, 103, 4) == ApplyResult::Inserted);
    CHECK(b.set_level(Side::Ask, 104, 3) == ApplyResult::Inserted);
    CHECK_EQ(b.best(Side::Ask).price_ticks, 103);
    // qty <= 0 never stores a level
    CHECK(b.set_level(Side::Ask, 103, 0) == ApplyResult::Removed);
    CHECK(b.set_level(Side::Ask, 200, 0) == ApplyResult::Missing);
    CHECK_EQ(b.depth(Side::Ask), static_cast<std::size_t>(2));
    CHECK(b.invariants_ok());
    b.clear();
    CHECK(b.empty(Side::Bid) && b.empty(Side::Ask));
}

TEST(book_top_n) {
    Book b;
    for (std::int64_t p = 1; p <= 5; ++p) {
        b.set_level(Side::Bid, 100 - p, p);
        b.set_level(Side::Ask, 100 + p, p * 10);
    }
    std::vector<Level> out{{1, 1}};
    b.top_n(Side::Bid, 3, out);
    CHECK_EQ(out.size(), static_cast<std::size_t>(3));
    CHECK(out[0] == (Level{99, 1}));
    CHECK(out[1] == (Level{98, 2}));
    CHECK(out[2] == (Level{97, 3}));
    b.top_n(Side::Ask, 10, out);  // fewer levels than N
    CHECK_EQ(out.size(), static_cast<std::size_t>(5));
    CHECK(out[0] == (Level{101, 10}));
    CHECK(out[4] == (Level{105, 50}));
    b.top_n(Side::Ask, 0, out);
    CHECK(out.empty());
    Book e;
    e.top_n(Side::Bid, 10, out);
    CHECK(out.empty());
}

TEST(book_crossed_locked) {
    Book b;
    CHECK(!b.crossed() && !b.locked());
    b.set_level(Side::Bid, 100, 1);
    CHECK(!b.crossed() && !b.locked());  // one side only
    b.set_level(Side::Ask, 101, 1);
    CHECK(!b.crossed() && !b.locked());
    b.set_level(Side::Ask, 100, 1);
    CHECK(b.locked() && !b.crossed());
    b.set_level(Side::Ask, 99, 1);
    CHECK(b.crossed() && !b.locked());
    CHECK(b.invariants_ok());  // the book stores what it is told
    b.remove_level(Side::Ask, 99);
    b.remove_level(Side::Ask, 100);
    CHECK(!b.crossed() && !b.locked());
}

TEST(book_load_and_equality) {
    Book a, b;
    a.load({{Side::Ask, {102, 1}}, {Side::Bid, {99, 2}}, {Side::Ask, {101, 3}}, {Side::Bid, {100, 4}}});
    b.set_level(Side::Bid, 100, 4);
    b.set_level(Side::Bid, 99, 2);
    b.set_level(Side::Ask, 101, 3);
    b.set_level(Side::Ask, 102, 1);
    CHECK(a == b);
    CHECK(a.invariants_ok());
    b.set_level(Side::Ask, 102, 2);
    CHECK(a != b);
    // duplicates in a load collapse (last wins) and zero-qty entries are skipped
    a.load({{Side::Bid, {100, 1}}, {Side::Bid, {100, 9}}, {Side::Bid, {98, 0}}});
    CHECK_EQ(a.depth(Side::Bid), static_cast<std::size_t>(1));
    CHECK_EQ(a.qty_at(Side::Bid, 100), 9);
    CHECK(a.empty(Side::Ask));
    CHECK(a.invariants_ok());
}

TEST(book_random_ops_match_std_map_model) {
    std::mt19937_64 rng(12345);
    Book book;
    std::map<std::int64_t, std::int64_t> bids, asks;  // price -> qty
    for (int i = 0; i < 10000; ++i) {
        const bool bid = (rng() & 1) != 0;
        const std::int64_t px = 1 + static_cast<std::int64_t>(rng() % 40);
        const int op = static_cast<int>(rng() % 3);
        auto& m = bid ? bids : asks;
        const Side s = bid ? Side::Bid : Side::Ask;
        if (op < 2) {
            const std::int64_t q = 1 + static_cast<std::int64_t>(rng() % 100);
            const bool had = m.count(px) != 0;
            const ApplyResult r = book.set_level(s, px, q);
            m[px] = q;
            CHECK(r == (had ? ApplyResult::Replaced : ApplyResult::Inserted));
        } else {
            const bool had = m.count(px) != 0;
            const ApplyResult r = book.remove_level(s, px);
            m.erase(px);
            CHECK(r == (had ? ApplyResult::Removed : ApplyResult::Missing));
        }
        CHECK(book.invariants_ok());
        if (i % 250 == 0) {
            std::vector<Level> got;
            book.top_n(Side::Bid, 100, got);
            std::vector<Level> want;
            for (auto it = bids.rbegin(); it != bids.rend(); ++it) want.push_back({it->first, it->second});
            CHECK(got == want);
            book.top_n(Side::Ask, 100, got);
            want.clear();
            for (const auto& kv : asks) want.push_back({kv.first, kv.second});
            CHECK(got == want);
            CHECK_EQ(book.depth(Side::Bid), bids.size());
            CHECK_EQ(book.depth(Side::Ask), asks.size());
        }
    }
}
