// Hand-built sequencing scenarios (DESIGN.md section 6 / 12.4). Generic scenarios run for both
// venues: logical update n is A diff (n,n) or B id n with prev n-1.
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "harness.hpp"
#include "md/session.hpp"

using namespace md;

namespace {

struct L {
    Side side;
    std::int64_t px, qty;
};

struct Gen {
    explicit Gen(Venue venue) : v(venue) {}
    Venue v;
    std::int64_t idx = 0;
    std::int64_t ts = 1000;

    Event base() {
        Event e;
        e.venue = v;
        e.msg_index = idx;
        e.exch_ts_ns = ts;
        e.local_ts_ns = ts + 10;
        return e;
    }
    // Update message covering sequence first..last (B: first == last == id, prev given).
    std::vector<Event> update(std::int64_t first, std::int64_t last, std::int64_t prev,
                              std::initializer_list<L> levels) {
        std::vector<Event> out;
        for (const L& l : levels) {
            Event e = base();
            e.type = l.qty > 0 ? EventType::Set : EventType::Delete;
            e.side = l.side;
            e.price_ticks = l.px;
            e.qty_lots = l.qty;
            e.seq_first = first;
            e.seq_last = last;
            e.prev_seq = v == Venue::B ? prev : kNone;
            out.push_back(e);
        }
        out.back().flags = kLastInMsg;
        ++idx;
        ts += 100;
        return out;
    }
    // Logical update n (contiguous with n-1).
    std::vector<Event> upd(std::int64_t n, std::initializer_list<L> levels) {
        return update(n, n, n - 1, levels);
    }
    std::vector<Event> snapshot(std::int64_t seq, std::initializer_list<L> levels) {
        std::vector<Event> out;
        Event b = base();
        b.type = EventType::SnapshotBegin;
        b.qty_lots = static_cast<std::int64_t>(levels.size());
        b.seq_first = b.seq_last = seq;
        out.push_back(b);
        for (const L& l : levels) {
            Event e = base();
            e.type = EventType::SnapshotLevel;
            e.side = l.side;
            e.price_ticks = l.px;
            e.qty_lots = l.qty;
            e.seq_first = e.seq_last = seq;
            out.push_back(e);
        }
        Event en = base();
        en.type = EventType::SnapshotEnd;
        en.seq_first = en.seq_last = seq;
        out.push_back(en);
        out.back().flags = kLastInMsg;
        ++idx;
        ts += 100;
        return out;
    }
    std::vector<Event> trade(std::int64_t tid) {
        Event e = base();
        e.type = EventType::Trade;
        e.side = Side::Bid;
        e.price_ticks = 101;
        e.qty_lots = 1;
        e.seq_first = e.seq_last = tid;
        e.flags = kLastInMsg;
        ++idx;
        ts += 100;
        return {e};
    }
};

void feed(VenueSession& s, const std::vector<Event>& evs) {
    for (const Event& e : evs) s.on_event(e);
}

// Verifies the listed counters, and that every counter not listed is zero (except the
// bookkeeping ones: messages, events, snapshots, trades, buffered_max).
void expect(const VenueSession& s, std::initializer_list<std::pair<const char*, std::int64_t>> want) {
    const VenueCounters& c = s.counters();
    for (std::size_t i = 0; i < kNumCounters; ++i) {
        const std::string name = counter_name(i);
        std::int64_t w = 0;
        bool listed = false;
        for (const auto& kv : want)
            if (name == kv.first) { w = kv.second; listed = true; }
        const bool bookkeeping = name == "messages" || name == "events" || name == "snapshots" ||
                                 name == "trades" || name == "buffered_max";
        if (!listed && bookkeeping) continue;
        ++mdtest::checks();
        if (counter_value(c, i) != w) {
            ++mdtest::failures();
            std::cerr << "counter " << name << " = " << counter_value(c, i) << ", expected " << w << "\n";
        }
    }
    for (const auto& kv : want) {
        std::int64_t v = 0;
        CHECK(counter_by_name(c, kv.first, v));
    }
}

// Base image used by the scenarios: bids 100:5 99:3, asks 101:2 102:4.
const std::initializer_list<L> kImage = {{Side::Bid, 100, 5}, {Side::Bid, 99, 3},
                                         {Side::Ask, 101, 2}, {Side::Ask, 102, 4}};

constexpr Venue kVenues[2] = {Venue::A, Venue::B};

}  // namespace

TEST(session_in_sync_apply) {
    for (Venue v : kVenues) {
        Gen g(v);
        VenueSession s(v);
        CHECK(s.state() == SessionState::Init);
        CHECK_EQ(s.last_applied(), -1);
        CHECK(!s.top().live);
        feed(s, g.snapshot(100, kImage));
        CHECK(s.state() == SessionState::Live);
        CHECK_EQ(s.last_applied(), 100);
        CHECK(s.top() == (TopOfBook{true, 100, 5, 101, 2}));
        feed(s, g.upd(101, {{Side::Ask, 101, 7}}));
        feed(s, g.upd(102, {{Side::Bid, 100, 1}, {Side::Ask, 102, 0}}));  // shrinks bid, deletes ask
        CHECK_EQ(s.last_applied(), 102);
        CHECK(s.top() == (TopOfBook{true, 100, 1, 101, 7}));
        CHECK_EQ(s.book().depth(Side::Ask), static_cast<std::size_t>(1));
        expect(s, {{"messages", 3}, {"snapshots", 1}});
        CHECK(s.book().invariants_ok());
    }
}

TEST(session_on_event_return_and_message_counting) {
    Gen g(Venue::A);
    VenueSession s(Venue::A);
    const auto snap = g.snapshot(100, kImage);
    int lasts = 0;
    for (const Event& e : snap) lasts += s.on_event(e) ? 1 : 0;
    CHECK_EQ(lasts, 1);
    CHECK_EQ(s.counters().events, static_cast<std::int64_t>(snap.size()));
    CHECK_EQ(s.counters().messages, 1);
    s.note_parse_error();
    CHECK_EQ(s.counters().parse_errors, 1);
    CHECK_EQ(s.counters().messages, 2);
    feed(s, g.trade(1));
    CHECK_EQ(s.counters().trades, 1);
    CHECK_EQ(s.counters().messages, 3);
}

TEST(session_duplicate_in_live) {
    for (Venue v : kVenues) {
        Gen g(v);
        VenueSession s(v);
        feed(s, g.snapshot(100, kImage));
        const auto u = g.upd(101, {{Side::Ask, 101, 7}});
        feed(s, u);
        const Book before = s.book();
        auto copy = u;
        for (Event& e : copy) e.msg_index = 99;
        feed(s, copy);                       // same sequence again
        CHECK(s.book() == before);
        CHECK(s.state() == SessionState::Live);
        CHECK_EQ(s.last_applied(), 101);
        expect(s, {{"duplicates_ignored", 1}});
    }
}

TEST(session_drop_gap_stale_resync) {
    for (Venue v : kVenues) {
        Gen g(v);
        VenueSession s(v);
        feed(s, g.snapshot(100, kImage));
        feed(s, g.upd(101, {{Side::Ask, 101, 7}}));
        // 102 is dropped
        feed(s, g.update(103, 103, 102, {{Side::Ask, 101, 9}}));
        CHECK(s.state() == SessionState::Stale);
        CHECK(s.stale_reason() == StaleReason::Gap);
        CHECK(!s.top().live);
        CHECK(s.top() == TopOfBook{});                   // no top of book is emitted while stale
        CHECK_EQ(s.book().qty_at(Side::Ask, 101), 7);    // the gapped update was not applied
        feed(s, g.upd(104, {{Side::Bid, 100, 8}}));      // buffered too
        CHECK(!s.top().live);
        feed(s, g.trade(5));                             // trades pass through while stale
        CHECK(s.state() == SessionState::Stale);
        // snapshot at 104 supersedes everything that was buffered
        feed(s, g.snapshot(104, {{Side::Bid, 100, 8}, {Side::Ask, 101, 9}}));
        CHECK(s.state() == SessionState::Live);
        CHECK(s.stale_reason() == StaleReason::None);
        CHECK_EQ(s.last_applied(), 104);
        CHECK(s.top() == (TopOfBook{true, 100, 8, 101, 9}));
        expect(s, {{"gaps", 1}, {"resyncs", 1}, {"checkpoints_skipped_stale", 1},
                   {"buffered_dropped", 2}, {"buffered_max", 2}});
    }
}

TEST(session_swap_is_out_of_order) {
    for (Venue v : kVenues) {
        Gen g(v);
        VenueSession s(v);
        feed(s, g.snapshot(100, kImage));
        feed(s, g.upd(101, {{Side::Ask, 101, 7}}));
        const auto u102 = g.upd(102, {{Side::Ask, 101, 8}});
        const auto u103 = g.upd(103, {{Side::Ask, 101, 9}});
        feed(s, u103);   // later message first: fails continuity
        CHECK(s.state() == SessionState::Stale);
        feed(s, u102);   // lower sequence than one already received
        CHECK(!s.top().live);
        feed(s, g.snapshot(103, {{Side::Bid, 100, 5}, {Side::Ask, 101, 9}}));
        CHECK(s.state() == SessionState::Live);
        expect(s, {{"gaps", 1}, {"out_of_order", 1}, {"resyncs", 1},
                   {"checkpoints_skipped_stale", 1}, {"buffered_dropped", 2}});
    }
}

TEST(session_duplicate_while_stale) {
    for (Venue v : kVenues) {
        Gen g(v);
        VenueSession s(v);
        feed(s, g.snapshot(100, kImage));
        const auto u = g.update(105, 105, 104, {{Side::Ask, 101, 9}});
        feed(s, u);
        feed(s, u);  // same key, buffered once
        expect(s, {{"gaps", 1}, {"duplicates_ignored", 1}, {"buffered_max", 1}});
    }
}

TEST(session_crossed_update) {
    for (Venue v : kVenues) {
        Gen g(v);
        VenueSession s(v);
        feed(s, g.snapshot(100, kImage));
        feed(s, g.upd(101, {{Side::Bid, 102, 700}}));  // bid above best ask 101
        CHECK(s.state() == SessionState::Stale);
        CHECK(s.stale_reason() == StaleReason::Crossed);
        CHECK_EQ(s.last_applied(), 101);               // it was in sequence, so it was applied
        CHECK(s.book().crossed());
        CHECK(!s.top().live);
        feed(s, g.upd(102, {{Side::Bid, 102, 0}}));    // would fix the book, but it is buffered
        CHECK(!s.top().live);
        feed(s, g.snapshot(102, kImage));
        CHECK(s.state() == SessionState::Live);
        CHECK(s.top() == (TopOfBook{true, 100, 5, 101, 2}));
        expect(s, {{"crossed_detected", 1}, {"resyncs", 1}, {"checkpoints_skipped_stale", 1},
                   {"buffered_dropped", 1}});
    }
}

TEST(session_locked_update) {
    for (Venue v : kVenues) {
        Gen g(v);
        VenueSession s(v);
        feed(s, g.snapshot(100, kImage));
        feed(s, g.upd(101, {{Side::Bid, 101, 4}}));    // bid == best ask
        CHECK(s.state() == SessionState::Stale);
        CHECK(s.stale_reason() == StaleReason::Locked);
        CHECK(!s.top().live);
        expect(s, {{"locked_detected", 1}});
        feed(s, g.snapshot(101, kImage));
        CHECK(s.state() == SessionState::Live);
        expect(s, {{"locked_detected", 1}, {"resyncs", 1}, {"checkpoints_skipped_stale", 1}});
    }
}

TEST(session_snapshot_in_live) {
    for (Venue v : kVenues) {
        Gen g(v);
        VenueSession s(v);
        feed(s, g.snapshot(100, kImage));
        feed(s, g.upd(101, {{Side::Ask, 101, 7}}));
        feed(s, g.upd(102, {{Side::Ask, 101, 8}}));
        // S < last: ignored, book untouched
        feed(s, g.snapshot(101, kImage));
        CHECK_EQ(s.book().qty_at(Side::Ask, 101), 8);
        CHECK_EQ(s.last_applied(), 102);
        // S == last, equal image: matched
        feed(s, g.snapshot(102, {{Side::Bid, 100, 5}, {Side::Bid, 99, 3}, {Side::Ask, 101, 8}, {Side::Ask, 102, 4}}));
        // S == last, different image: mismatched, venue image adopted
        feed(s, g.snapshot(102, {{Side::Bid, 100, 6}, {Side::Ask, 101, 8}}));
        CHECK_EQ(s.book().qty_at(Side::Bid, 100), 6);
        CHECK_EQ(s.book().depth(Side::Ask), static_cast<std::size_t>(1));
        CHECK(s.state() == SessionState::Live);
        expect(s, {{"snapshots_ignored_late", 1}, {"checkpoints_matched", 1}, {"checkpoints_mismatched", 1}});
        // S > last in LIVE: silent loss -> gap + resync
        feed(s, g.snapshot(110, {{Side::Bid, 100, 1}, {Side::Ask, 101, 1}}));
        CHECK(s.state() == SessionState::Live);
        CHECK_EQ(s.last_applied(), 110);
        expect(s, {{"snapshots_ignored_late", 1}, {"checkpoints_matched", 1}, {"checkpoints_mismatched", 1},
                   {"gaps", 1}, {"resyncs", 1}, {"checkpoints_skipped_stale", 1}});
    }
}

TEST(session_snapshot_matching_but_crossed_goes_stale) {
    Gen g(Venue::A);
    VenueSession s(Venue::A);
    feed(s, g.snapshot(100, kImage));
    // a venue image at S == last that is itself crossed: adopted, then flagged
    feed(s, g.snapshot(100, {{Side::Bid, 105, 1}, {Side::Ask, 101, 1}}));
    CHECK(s.state() == SessionState::Stale);
    CHECK(s.stale_reason() == StaleReason::Crossed);
    expect(s, {{"checkpoints_mismatched", 1}, {"crossed_detected", 1}});
}

TEST(session_init_buffers_and_applies_continuous_updates) {
    for (Venue v : kVenues) {
        Gen g(v);
        VenueSession s(v);
        feed(s, g.upd(101, {{Side::Ask, 101, 7}}));  // before any snapshot
        feed(s, g.upd(102, {{Side::Bid, 100, 9}}));
        CHECK(s.state() == SessionState::Init);
        CHECK(!s.top().live);
        feed(s, g.snapshot(100, kImage));
        CHECK(s.state() == SessionState::Live);
        CHECK_EQ(s.last_applied(), 102);
        CHECK_EQ(s.book().qty_at(Side::Ask, 101), 7);
        CHECK_EQ(s.book().qty_at(Side::Bid, 100), 9);
        // INIT -> LIVE is not a resync and not a checkpoint
        expect(s, {{"buffered_max", 2}});
    }
}

// Buffered straddle, Venue A: buffered (101-103), (104-106), (107-107); snapshot S=104.
TEST(session_buffered_straddle_a) {
    Gen g(Venue::A);
    VenueSession s(Venue::A);
    feed(s, g.snapshot(99, {{Side::Bid, 98, 1}, {Side::Ask, 110, 1}}));
    feed(s, g.update(101, 103, kNone, {{Side::Bid, 98, 2}}));          // gap: 100 missing
    CHECK(s.state() == SessionState::Stale);
    feed(s, g.update(104, 106, kNone, {{Side::Bid, 97, 7}}));
    feed(s, g.update(107, 107, kNone, {{Side::Ask, 110, 9}}));
    CHECK(!s.top().live);
    feed(s, g.snapshot(104, {{Side::Bid, 98, 5}, {Side::Ask, 110, 1}}));
    CHECK(s.state() == SessionState::Live);
    CHECK_EQ(s.last_applied(), 107);
    CHECK_EQ(s.book().qty_at(Side::Bid, 98), 5);   // from the snapshot, (101-103) was dropped
    CHECK_EQ(s.book().qty_at(Side::Bid, 97), 7);   // (104-106) applied: 104 <= 105 <= 106
    CHECK_EQ(s.book().qty_at(Side::Ask, 110), 9);  // (107) applied
    expect(s, {{"gaps", 1}, {"resyncs", 1}, {"checkpoints_skipped_stale", 1},
               {"buffered_dropped", 1}, {"buffered_max", 3}});
    // and the session continues cleanly
    feed(s, g.update(108, 108, kNone, {{Side::Bid, 97, 8}}));
    CHECK_EQ(s.last_applied(), 108);
    CHECK_EQ(s.book().qty_at(Side::Bid, 97), 8);
}

// Same idea for Venue B: ids chain through prev_id; ids need not be contiguous.
TEST(session_buffered_straddle_b) {
    Gen g(Venue::B);
    VenueSession s(Venue::B);
    feed(s, g.snapshot(100, {{Side::Bid, 98, 1}, {Side::Ask, 110, 1}}));
    feed(s, g.update(102, 102, 101, {{Side::Bid, 98, 2}}));            // prev 101 != 100: gap
    CHECK(s.state() == SessionState::Stale);
    feed(s, g.update(104, 104, 102, {{Side::Bid, 97, 7}}));
    feed(s, g.update(105, 105, 104, {{Side::Ask, 110, 9}}));
    feed(s, g.snapshot(102, {{Side::Bid, 98, 5}, {Side::Ask, 110, 1}}));
    CHECK(s.state() == SessionState::Live);
    CHECK_EQ(s.last_applied(), 105);
    CHECK_EQ(s.book().qty_at(Side::Bid, 98), 5);   // id 102 is at the snapshot: dropped
    CHECK_EQ(s.book().qty_at(Side::Bid, 97), 7);   // 104.prev == 102: applied
    CHECK_EQ(s.book().qty_at(Side::Ask, 110), 9);  // 105.prev == 104: applied
    expect(s, {{"gaps", 1}, {"resyncs", 1}, {"checkpoints_skipped_stale", 1},
               {"buffered_dropped", 1}, {"buffered_max", 3}});
}

TEST(session_hole_after_snapshot_stays_stale_a) {
    Gen g(Venue::A);
    VenueSession s(Venue::A);
    feed(s, g.snapshot(99, kImage));
    feed(s, g.update(101, 103, kNone, {{Side::Bid, 100, 2}}));          // gap #1
    feed(s, g.update(110, 112, kNone, {{Side::Bid, 100, 3}}));
    feed(s, g.snapshot(104, kImage));                                     // 110 is not continuous with 104
    CHECK(s.state() == SessionState::Stale);
    CHECK(s.stale_reason() == StaleReason::Gap);
    CHECK(!s.top().live);
    CHECK(s.book().empty(Side::Bid) && s.book().empty(Side::Ask));
    expect(s, {{"gaps", 2}, {"checkpoints_skipped_stale", 1}, {"buffered_dropped", 1}, {"buffered_max", 2}});
    // the leftover message is still buffered; a later snapshot at 112 recovers
    feed(s, g.snapshot(112, {{Side::Bid, 100, 3}, {Side::Ask, 101, 2}}));
    CHECK(s.state() == SessionState::Live);
    CHECK_EQ(s.last_applied(), 112);
    CHECK(s.top() == (TopOfBook{true, 100, 3, 101, 2}));
    expect(s, {{"gaps", 2}, {"resyncs", 1}, {"checkpoints_skipped_stale", 2}, {"buffered_dropped", 2},
               {"buffered_max", 2}});
}

TEST(session_hole_after_snapshot_stays_stale_b) {
    Gen g(Venue::B);
    VenueSession s(Venue::B);
    feed(s, g.snapshot(100, kImage));
    feed(s, g.update(103, 103, 102, {{Side::Bid, 100, 2}}));            // gap #1
    feed(s, g.update(109, 109, 107, {{Side::Bid, 100, 3}}));
    feed(s, g.snapshot(103, kImage));                                     // 109.prev = 107 != 103
    CHECK(s.state() == SessionState::Stale);
    CHECK(!s.top().live);
    expect(s, {{"gaps", 2}, {"checkpoints_skipped_stale", 1}, {"buffered_dropped", 1}, {"buffered_max", 2}});
    feed(s, g.snapshot(109, {{Side::Bid, 100, 3}, {Side::Ask, 101, 2}}));
    CHECK(s.state() == SessionState::Live);
    expect(s, {{"gaps", 2}, {"resyncs", 1}, {"checkpoints_skipped_stale", 2}, {"buffered_dropped", 2},
               {"buffered_max", 2}});
}

TEST(session_buffer_overflow) {
    for (Venue v : kVenues) {
        Gen g(v);
        SessionConfig cfg;
        cfg.max_buffered_messages = 2;
        VenueSession s(v, cfg);
        feed(s, g.snapshot(100, kImage));
        feed(s, g.update(105, 105, 104, {{Side::Bid, 100, 1}}));  // gap, buffer 1
        feed(s, g.update(106, 106, 105, {{Side::Bid, 100, 2}}));  // buffer 2
        expect(s, {{"gaps", 1}, {"buffered_max", 2}});
        feed(s, g.update(107, 107, 106, {{Side::Bid, 100, 3}}));  // full: clear, count, buffer
        expect(s, {{"gaps", 1}, {"buffered_max", 2}, {"buffer_overflows", 1}});
        feed(s, g.snapshot(107, kImage));                          // the lost messages cannot hurt: snapshot covers
        CHECK(s.state() == SessionState::Live);
        expect(s, {{"gaps", 1}, {"resyncs", 1}, {"checkpoints_skipped_stale", 1}, {"buffered_max", 2},
                   {"buffer_overflows", 1}, {"buffered_dropped", 1}});
    }
}

TEST(session_aborted_snapshot) {
    for (Venue v : kVenues) {
        Gen g(v);
        VenueSession s(v);
        feed(s, g.snapshot(100, kImage));
        // build SB + 1 SL, then an update arrives before SE
        auto snap = g.snapshot(101, {{Side::Bid, 100, 5}, {Side::Ask, 101, 2}});
        s.on_event(snap[0]);
        s.on_event(snap[1]);
        feed(s, g.upd(101, {{Side::Ask, 101, 7}}));
        CHECK_EQ(s.counters().snapshot_errors, 1);
        CHECK_EQ(s.book().qty_at(Side::Ask, 101), 7);  // the update applied normally
        s.on_event(snap[2]);                            // remaining SL
        s.on_event(snap[3]);                            // SE with no pending snapshot
        expect(s, {{"snapshot_errors", 3}});
        CHECK(s.state() == SessionState::Live);
        CHECK_EQ(s.counters().snapshots, 1);            // only the first snapshot completed
    }
}

TEST(session_snapshot_count_and_id_errors) {
    Gen g(Venue::A);
    VenueSession s(Venue::A);
    // count says 3, only 2 levels arrive
    auto snap = g.snapshot(100, {{Side::Bid, 100, 5}, {Side::Ask, 101, 2}});
    snap[0].qty_lots = 3;
    feed(s, snap);
    CHECK(s.state() == SessionState::Init);
    expect(s, {{"snapshot_errors", 1}});
    // level with the wrong snapshot id
    auto snap2 = g.snapshot(100, {{Side::Bid, 100, 5}, {Side::Ask, 101, 2}});
    snap2[1].seq_first = snap2[1].seq_last = 99;
    feed(s, snap2);
    CHECK(s.state() == SessionState::Init);
    // a new SB before the previous one ended counts, and the new one wins
    auto a = g.snapshot(100, {{Side::Bid, 100, 5}, {Side::Ask, 101, 2}});
    auto b = g.snapshot(100, {{Side::Bid, 100, 5}, {Side::Ask, 101, 2}});
    s.on_event(a[0]);
    feed(s, b);
    CHECK(s.state() == SessionState::Live);
    CHECK(s.counters().snapshot_errors >= 3);
}

TEST(session_book_anomaly_counters) {
    // Venue B event types: Add on existing, Modify missing, Delete missing.
    Gen g(Venue::B);
    VenueSession s(Venue::B);
    feed(s, g.snapshot(100, kImage));
    auto mk = [&](std::int64_t id, EventType t, Side sd, std::int64_t px, std::int64_t q) {
        Event e = g.base();
        e.type = t;
        e.side = sd;
        e.price_ticks = px;
        e.qty_lots = q;
        e.seq_first = e.seq_last = id;
        e.prev_seq = id - 1;
        e.flags = kLastInMsg;
        ++g.idx;
        return e;
    };
    s.on_event(mk(101, EventType::Add, Side::Bid, 100, 6));      // exists: replace + count
    CHECK_EQ(s.book().qty_at(Side::Bid, 100), 6);
    s.on_event(mk(102, EventType::Modify, Side::Bid, 98, 2));    // missing: insert + count
    CHECK_EQ(s.book().qty_at(Side::Bid, 98), 2);
    s.on_event(mk(103, EventType::Delete, Side::Bid, 50, 0));    // missing: no-op + count
    s.on_event(mk(104, EventType::Add, Side::Bid, 97, 1));       // clean add
    s.on_event(mk(105, EventType::Modify, Side::Bid, 97, 3));    // clean modify
    s.on_event(mk(106, EventType::Delete, Side::Bid, 97, 0));    // clean delete
    CHECK(s.state() == SessionState::Live);
    CHECK_EQ(s.last_applied(), 106);
    expect(s, {{"add_on_existing", 1}, {"modify_missing", 1}, {"delete_missing", 1}});
    CHECK(s.book().invariants_ok());

    // Venue A: Delete of an absent level is counted, Set upserts silently.
    Gen ga(Venue::A);
    VenueSession sa(Venue::A);
    feed(sa, ga.snapshot(100, kImage));
    feed(sa, ga.upd(101, {{Side::Bid, 90, 0}}));
    feed(sa, ga.upd(102, {{Side::Bid, 95, 2}}));
    expect(sa, {{"delete_missing", 1}});
}

TEST(session_hook_sees_pre_snapshot_book) {
    Gen g(Venue::A);
    VenueSession s(Venue::A);
    std::vector<std::pair<std::int64_t, std::int64_t>> calls;
    std::vector<std::int64_t> bid_qtys;
    s.set_before_snapshot_hook([&](const VenueSession& sess, std::int64_t seq, std::int64_t begin_idx) {
        calls.emplace_back(seq, begin_idx);
        bid_qtys.push_back(sess.book().qty_at(Side::Bid, 100));
    });
    g.idx = 7;
    feed(s, g.snapshot(100, kImage));
    feed(s, g.upd(101, {{Side::Bid, 100, 9}}));
    g.idx = 20;
    feed(s, g.snapshot(101, {{Side::Bid, 100, 9}, {Side::Ask, 101, 2}}));
    CHECK_EQ(calls.size(), static_cast<std::size_t>(2));
    CHECK_EQ(calls[0].first, 100);
    CHECK_EQ(calls[0].second, 7);
    CHECK_EQ(calls[1].first, 101);
    CHECK_EQ(calls[1].second, 20);
    CHECK_EQ(bid_qtys[0], 0);  // book empty before the initial image
    CHECK_EQ(bid_qtys[1], 9);  // live book, before the checkpoint replaces it
}

TEST(session_trades_do_not_touch_state) {
    for (Venue v : kVenues) {
        Gen g(v);
        VenueSession s(v);
        feed(s, g.trade(1));  // INIT
        CHECK(s.state() == SessionState::Init);
        feed(s, g.snapshot(100, kImage));
        const Book before = s.book();
        feed(s, g.trade(999999));  // trade ids are their own namespace
        feed(s, g.trade(2));
        CHECK(s.book() == before);
        CHECK_EQ(s.last_applied(), 100);
        expect(s, {{"trades", 3}});
    }
}
