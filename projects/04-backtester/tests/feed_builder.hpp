// Builds Venue A style md::Event streams in code: one snapshot message, SET/DELETE depth messages and
// one-event TRADE messages. Times are ns; use ms() / us() for readability.
#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include "bt/latency.hpp"
#include "md/types.hpp"

namespace fb {

constexpr std::int64_t us(std::int64_t v) { return v * 1000; }
constexpr std::int64_t ms(std::int64_t v) { return v * 1'000'000; }

using md::Side;
using Lv = std::pair<std::int64_t, std::int64_t>;  // price, qty

struct Op {
    Side side;
    std::int64_t px;
    std::int64_t qty;  // 0 = DELETE
};

struct Builder {
    std::vector<md::Event> ev;
    std::int64_t seq = 1000000;  // last applied update sequence number
    std::int64_t msg = 0;
    std::int64_t tid = 0;

    md::Event base(std::int64_t ts, md::EventType t, Side s, std::int64_t px, std::int64_t qty,
                   std::int64_t sf, std::int64_t sl) const {
        md::Event e;
        e.venue = md::Venue::A;
        e.type = t;
        e.side = s;
        e.price_ticks = px;
        e.qty_lots = qty;
        e.local_ts_ns = ts;
        e.exch_ts_ns = ts - 100'000;
        e.seq_first = sf;
        e.seq_last = sl;
        e.prev_seq = md::kNone;
        e.msg_index = msg;
        e.flags = 0;
        return e;
    }

    // A full snapshot message at the current sequence number.
    void snapshot(std::int64_t ts, const std::vector<Lv>& bids, const std::vector<Lv>& asks) {
        ev.push_back(base(ts, md::EventType::SnapshotBegin, Side::None, 0,
                          static_cast<std::int64_t>(bids.size() + asks.size()), seq, seq));
        for (const Lv& l : bids) ev.push_back(base(ts, md::EventType::SnapshotLevel, Side::Bid, l.first, l.second, seq, seq));
        for (const Lv& l : asks) ev.push_back(base(ts, md::EventType::SnapshotLevel, Side::Ask, l.first, l.second, seq, seq));
        ev.push_back(base(ts, md::EventType::SnapshotEnd, Side::None, 0, 0, seq, seq));
        ev.back().flags = md::kLastInMsg;
        ++msg;
    }

    // One depth message holding several ops (sequence number advances by one).
    void depth(std::int64_t ts, const std::vector<Op>& ops) {
        ++seq;
        for (const Op& o : ops)
            ev.push_back(base(ts, o.qty > 0 ? md::EventType::Set : md::EventType::Delete, o.side, o.px,
                              o.qty > 0 ? o.qty : 0, seq, seq));
        ev.back().flags = md::kLastInMsg;
        ++msg;
    }
    void set(std::int64_t ts, Side s, std::int64_t px, std::int64_t qty) { depth(ts, {Op{s, px, qty}}); }
    void del(std::int64_t ts, Side s, std::int64_t px) { depth(ts, {Op{s, px, 0}}); }

    // `aggressor` = side of the aggressing order (Bid = buyer lifted the ask).
    void trade(std::int64_t ts, Side aggressor, std::int64_t px, std::int64_t qty) {
        ++tid;
        md::Event e = base(ts, md::EventType::Trade, aggressor, px, qty, tid, tid);
        e.flags = md::kLastInMsg;
        ev.push_back(e);
        ++msg;
    }
    // Convenience: sell aggressor hits the bid at px.
    void sell(std::int64_t ts, std::int64_t px, std::int64_t qty) { trade(ts, Side::Ask, px, qty); }
    void buy(std::int64_t ts, std::int64_t px, std::int64_t qty) { trade(ts, Side::Bid, px, qty); }

    // Skips `n` sequence numbers: the next depth message then opens a gap.
    void skip_seq(std::int64_t n) { seq += n; }
};

// A legal Venue A style random walk: touch moves that delete and add levels in one message, random SETs
// near the touch, and trades at the touch each followed by its reduction message 1 us later.
inline std::vector<md::Event> random_feed(std::uint64_t seed, int n_steps) {
    bt::SplitMix64 rng(seed);
    auto rnd = [&rng](std::int64_t lo, std::int64_t hi) {  // inclusive
        return lo + static_cast<std::int64_t>(rng.next() % static_cast<std::uint64_t>(hi - lo + 1));
    };
    std::map<std::int64_t, std::int64_t> bids, asks;  // px -> qty
    std::int64_t bb = 10000, spread = 1;
    constexpr int kLevels = 5;
    auto fill_side = [&](bool bid, std::int64_t touch) {
        std::map<std::int64_t, std::int64_t> out;
        auto& old = bid ? bids : asks;
        for (int i = 0; i < kLevels; ++i) {
            const std::int64_t px = bid ? touch - i : touch + i;
            const auto it = old.find(px);
            out[px] = it != old.end() ? it->second : rnd(100, 5000);
        }
        old = out;
    };
    fill_side(true, bb);
    fill_side(false, bb + spread);
    Builder b;
    std::int64_t ts = 1'000'000'000'000;
    {
        std::vector<Lv> bl(bids.rbegin(), bids.rend()), al(asks.begin(), asks.end());
        b.snapshot(ts, bl, al);
    }
    // Emit a message that turns the current model book into (nb, na) touches.
    auto rebuild = [&](std::int64_t t, std::int64_t nbid, std::int64_t nask) {
        const auto ob = bids, oa = asks;
        fill_side(true, nbid);
        fill_side(false, nask);
        std::vector<Op> ops;
        for (const auto& l : ob)
            if (bids.count(l.first) == 0) ops.push_back({Side::Bid, l.first, 0});
        for (const auto& l : oa)
            if (asks.count(l.first) == 0) ops.push_back({Side::Ask, l.first, 0});
        for (const auto& l : bids)
            if (ob.count(l.first) == 0) ops.push_back({Side::Bid, l.first, l.second});
        for (const auto& l : asks)
            if (oa.count(l.first) == 0) ops.push_back({Side::Ask, l.first, l.second});
        if (!ops.empty()) b.depth(t, ops);
    };
    for (int step = 0; step < n_steps; ++step) {
        ts += us(rnd(500, 5000));
        const std::int64_t u = rnd(0, 99);
        const std::int64_t best_bid = bids.rbegin()->first, best_ask = asks.begin()->first;
        if (u < 45) {  // trade at the touch + reduction
            const bool buyer = rnd(0, 1) == 1;
            auto& side = buyer ? asks : bids;
            const auto it = buyer ? side.begin() : std::prev(side.end());
            const std::int64_t px = it->first, q = it->second;
            const std::int64_t v = rnd(1, q);
            b.trade(ts, buyer ? Side::Bid : Side::Ask, px, v);
            ts += us(1);
            if (v == q) {
                side.erase(it);
                std::vector<Op> ops{{buyer ? Side::Ask : Side::Bid, px, 0}};
                while (side.size() < static_cast<std::size_t>(kLevels)) {  // keep depth: add a far level
                    const std::int64_t far = buyer ? side.rbegin()->first + 1 : side.begin()->first - 1;
                    const std::int64_t fq = rnd(100, 5000);
                    side[far] = fq;
                    ops.push_back({buyer ? Side::Ask : Side::Bid, far, fq});
                }
                b.depth(ts, ops);
            } else {
                it->second = q - v;
                b.set(ts, buyer ? Side::Ask : Side::Bid, px, q - v);
            }
        } else if (u < 70) {  // re-size one of the top three levels
            const bool bid = rnd(0, 1) == 1;
            auto& side = bid ? bids : asks;
            auto it = bid ? std::prev(side.end()) : side.begin();
            const std::int64_t k = rnd(0, 2);
            for (std::int64_t i = 0; i < k; ++i) it = bid ? std::prev(it) : std::next(it);
            const std::int64_t q = rnd(100, 5000);
            it->second = q;
            b.set(ts, bid ? Side::Bid : Side::Ask, it->first, q);
        } else if (u < 85) {  // touch move: delete and add levels in one message
            const std::int64_t nb = std::clamp<std::int64_t>(best_bid + (rnd(0, 1) == 1 ? 1 : -1), 9500, 10500);
            const std::int64_t sp = rnd(1, 3);
            (void)best_ask;
            spread = sp;
            rebuild(ts, nb, nb + sp);
        } else {  // deep level re-size
            const bool bid = rnd(0, 1) == 1;
            auto& side = bid ? bids : asks;
            auto it = bid ? std::prev(side.end(), 4) : std::next(side.begin(), 3);
            const std::int64_t q = rnd(100, 5000);
            it->second = q;
            b.set(ts, bid ? Side::Bid : Side::Ask, it->first, q);
        }
    }
    return b.ev;
}

}  // namespace fb
