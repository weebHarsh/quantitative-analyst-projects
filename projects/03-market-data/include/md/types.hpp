// Normalised event types shared by every module. All data is SYNTHETIC; see DESIGN.md.
#pragma once

#include <cstdint>
#include <string_view>

namespace md {

enum class Venue : std::uint8_t { A = 0, B = 1 };  // bit in masks: 1u << id
enum class Side : std::uint8_t { None = 0, Bid = 1, Ask = 2 };
enum class EventType : std::uint8_t {
    SnapshotBegin = 0, SnapshotLevel = 1, SnapshotEnd = 2,
    Add = 3, Modify = 4, Set = 5, Delete = 6, Trade = 7
};
constexpr std::uint8_t kLastInMsg = 1;  // Event::flags bit 0
constexpr std::int64_t kNone = -1;      // absent sequence field

struct Event {
    std::int64_t price_ticks = 0;  // 0 when not applicable
    std::int64_t qty_lots = 0;     // 0 when not applicable
    std::int64_t exch_ts_ns = 0;   // venue time in ns
    std::int64_t local_ts_ns = 0;  // local receive time in ns
    std::int64_t seq_first = kNone;
    std::int64_t seq_last = kNone;
    std::int64_t prev_seq = kNone;
    std::int64_t msg_index = 0;    // 0-based raw line index (Venue B: header excluded)
    Venue venue = Venue::A;
    EventType type = EventType::Set;
    Side side = Side::None;
    std::uint8_t flags = 0;        // kLastInMsg on the last event of a raw message
};

struct Level {
    std::int64_t price_ticks = 0;
    std::int64_t qty_lots = 0;
};
inline bool operator==(const Level& a, const Level& b) {
    return a.price_ticks == b.price_ticks && a.qty_lots == b.qty_lots;
}
inline bool operator!=(const Level& a, const Level& b) { return !(a == b); }

struct TopOfBook {
    bool live = false;
    std::int64_t bid_px = 0, bid_qty = 0, ask_px = 0, ask_qty = 0;
};
inline bool operator==(const TopOfBook& a, const TopOfBook& b) {
    return a.live == b.live && a.bid_px == b.bid_px && a.bid_qty == b.bid_qty &&
           a.ask_px == b.ask_px && a.ask_qty == b.ask_qty;
}
inline bool operator!=(const TopOfBook& a, const TopOfBook& b) { return !(a == b); }

struct MergedQuote {
    std::int64_t local_ts_ns = 0;
    std::int64_t bid_px = 0, bid_qty = 0;
    std::int64_t ask_px = 0, ask_qty = 0;
    std::uint8_t bid_venues = 0, ask_venues = 0, live_venues = 0;
};
// All fields except local_ts_ns.
inline bool same_quote(const MergedQuote& a, const MergedQuote& b) {
    return a.bid_px == b.bid_px && a.bid_qty == b.bid_qty && a.ask_px == b.ask_px &&
           a.ask_qty == b.ask_qty && a.bid_venues == b.bid_venues &&
           a.ask_venues == b.ask_venues && a.live_venues == b.live_venues;
}

inline const char* to_string(Venue v) { return v == Venue::A ? "A" : "B"; }
inline const char* to_string(Side s) {
    switch (s) {
        case Side::Bid: return "BID";
        case Side::Ask: return "ASK";
        default: return "NONE";
    }
}
inline const char* to_string(EventType t) {
    switch (t) {
        case EventType::SnapshotBegin: return "SNAPSHOT_BEGIN";
        case EventType::SnapshotLevel: return "SNAPSHOT_LEVEL";
        case EventType::SnapshotEnd: return "SNAPSHOT_END";
        case EventType::Add: return "ADD";
        case EventType::Modify: return "MODIFY";
        case EventType::Set: return "SET";
        case EventType::Delete: return "DELETE";
        case EventType::Trade: return "TRADE";
    }
    return "?";
}
inline bool parse_venue(std::string_view s, Venue& out) {
    if (s == "A") { out = Venue::A; return true; }
    if (s == "B") { out = Venue::B; return true; }
    return false;
}

}  // namespace md
