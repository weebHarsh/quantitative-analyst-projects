#include "md/parser_a.hpp"

#include <limits>

#include "md/fixed.hpp"

namespace md {
namespace {

enum Key : unsigned { kR, kE, kEv, kSeq, kFirst, kLast, kTid, kPx, kQty, kSide, kB, kA, kNumKeys };
enum Kind : unsigned { kUint, kString, kArray };

struct KeyDef {
    std::string_view name;
    Kind kind;
};
constexpr KeyDef kKeyDefs[kNumKeys] = {
    {"r", kUint},       {"e", kString},   {"E", kUint},   {"seq", kUint},
    {"first_seq", kUint}, {"last_seq", kUint}, {"tid", kUint}, {"px", kString},
    {"qty", kString},   {"side", kString}, {"b", kArray}, {"a", kArray},
};

constexpr unsigned bit(unsigned k) { return 1u << k; }
constexpr unsigned kSnapshotMask = bit(kR) | bit(kE) | bit(kEv) | bit(kSeq) | bit(kB) | bit(kA);
constexpr unsigned kDepthMask = bit(kR) | bit(kE) | bit(kEv) | bit(kFirst) | bit(kLast) | bit(kB) | bit(kA);
constexpr unsigned kTradeMask = bit(kR) | bit(kE) | bit(kEv) | bit(kTid) | bit(kPx) | bit(kQty) | bit(kSide);

struct Val {
    std::int64_t u = 0;
    std::string_view s;
};

// Reads a quoted string with no escapes starting at pos (line[pos] == '"'); advances pos.
bool read_string(std::string_view line, std::size_t& pos, std::string_view& out) {
    if (pos >= line.size() || line[pos] != '"') return false;
    const std::size_t close = line.find('"', pos + 1);
    if (close == std::string_view::npos) return false;
    out = line.substr(pos + 1, close - pos - 1);
    if (out.find('\\') != std::string_view::npos) return false;
    pos = close + 1;
    return true;
}

}  // namespace

bool ParserA::parse_array(std::string_view line, std::size_t& pos, std::vector<Entry>& dst) {
    if (pos >= line.size() || line[pos] != '[') return false;
    ++pos;
    if (pos < line.size() && line[pos] == ']') {
        ++pos;
        return true;
    }
    for (;;) {
        if (pos >= line.size() || line[pos] != '[') return false;
        ++pos;
        std::string_view p, q;
        if (!read_string(line, pos, p)) return false;
        if (pos >= line.size() || line[pos] != ',') return false;
        ++pos;
        if (!read_string(line, pos, q)) return false;
        if (pos >= line.size() || line[pos] != ']') return false;
        ++pos;
        dst.emplace_back(p, q);
        if (pos >= line.size()) return false;
        if (line[pos] == ']') {
            ++pos;
            return true;
        }
        if (line[pos] != ',') return false;
        ++pos;
    }
}

ParseStatus ParserA::parse(std::string_view line, std::int64_t msg_index, std::vector<Event>& out) {
    const std::size_t base = out.size();
    const ParseStatus st = parse_impl(line, msg_index, out);
    if (st == ParseStatus::Error) out.resize(base);
    return st;
}

ParseStatus ParserA::parse_impl(std::string_view line, std::int64_t msg_index,
                                std::vector<Event>& out) {
    err_.clear();
    bids_.clear();
    asks_.clear();
    for (char ch : line) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c <= 0x20 || c == 0x7f) return fail("whitespace or control character");
    }
    if (line.size() < 2 || line.front() != '{' || line.back() != '}') return fail("not a flat object");
    const std::size_t end = line.size() - 1;  // index of the closing brace
    if (end == 1) return fail("empty object");

    Val vals[kNumKeys];
    unsigned present = 0;
    std::size_t pos = 1;
    for (;;) {
        std::string_view key;
        if (!read_string(line, pos, key)) return fail("malformed key");
        unsigned k = kNumKeys;
        for (unsigned i = 0; i < kNumKeys; ++i)
            if (kKeyDefs[i].name == key) { k = i; break; }
        if (k == kNumKeys) return fail("unknown key");
        if ((present & bit(k)) != 0) return fail("duplicate key");
        present |= bit(k);
        if (pos >= end || line[pos] != ':') return fail("expected ':'");
        ++pos;
        if (pos >= end) return fail("missing value");
        const char c0 = line[pos];
        switch (kKeyDefs[k].kind) {
            case kUint: {
                if (c0 < '0' || c0 > '9') return fail("expected unsigned integer value");
                std::size_t e2 = pos;
                while (e2 < end && line[e2] >= '0' && line[e2] <= '9') ++e2;
                if (!parse_uint(line.substr(pos, e2 - pos), vals[k].u)) return fail("integer out of range");
                pos = e2;
                break;
            }
            case kString:
                if (c0 != '"') return fail("expected string value");
                if (!read_string(line, pos, vals[k].s)) return fail("malformed string");
                break;
            case kArray:
                if (c0 != '[') return fail("expected array value");
                if (!parse_array(line, pos, k == kB ? bids_ : asks_)) return fail("malformed level array");
                break;
        }
        if (pos > end) return fail("value overruns line");
        if (pos == end) break;
        if (line[pos] != ',') return fail("expected ',' between members");
        ++pos;
        if (pos >= end) return fail("trailing comma");
    }

    if ((present & bit(kE)) == 0) return fail("missing key 'e'");
    unsigned required = 0;
    const std::string_view type = vals[kE].s;
    if (type == "snapshot") required = kSnapshotMask;
    else if (type == "depth") required = kDepthMask;
    else if (type == "trade") required = kTradeMask;
    else return fail("unknown message type");
    if ((required & ~present) != 0) return fail("missing key");
    if ((present & ~required) != 0) return fail("key not valid for message type");

    constexpr std::int64_t kMaxUs = std::numeric_limits<std::int64_t>::max() / 1000;
    if (vals[kEv].u > kMaxUs) return fail("event time overflow");

    Event proto;
    proto.venue = Venue::A;
    proto.msg_index = msg_index;
    proto.exch_ts_ns = vals[kEv].u * 1000;
    proto.local_ts_ns = vals[kR].u;

    if (type == "trade") {
        Event e = proto;
        e.type = EventType::Trade;
        if (vals[kSide].s == "buy") e.side = Side::Bid;
        else if (vals[kSide].s == "sell") e.side = Side::Ask;
        else return fail("bad trade side");
        if (!parse_fixed(vals[kPx].s, 2, e.price_ticks)) return fail("bad price");
        if (!parse_fixed(vals[kQty].s, 3, e.qty_lots)) return fail("bad quantity");
        e.seq_first = e.seq_last = vals[kTid].u;
        e.flags = kLastInMsg;
        out.push_back(e);
        return ParseStatus::Ok;
    }

    const bool snap = (type == "snapshot");
    if (snap) {
        proto.seq_first = proto.seq_last = vals[kSeq].u;
    } else {
        if (vals[kFirst].u > vals[kLast].u) return fail("first_seq > last_seq");
        if (bids_.empty() && asks_.empty()) return fail("empty depth diff");
        proto.seq_first = vals[kFirst].u;
        proto.seq_last = vals[kLast].u;
    }

    if (snap) {
        Event b = proto;
        b.type = EventType::SnapshotBegin;
        b.qty_lots = static_cast<std::int64_t>(bids_.size() + asks_.size());
        out.push_back(b);
    }
    for (int side = 0; side < 2; ++side) {
        const auto& src = side == 0 ? bids_ : asks_;
        for (const Entry& en : src) {
            Event e = proto;
            e.side = side == 0 ? Side::Bid : Side::Ask;
            if (!parse_fixed(en.first, 2, e.price_ticks)) return fail("bad price");
            if (!parse_fixed(en.second, 3, e.qty_lots)) return fail("bad quantity");
            if (snap) {
                if (e.qty_lots <= 0) return fail("snapshot level with zero quantity");
                e.type = EventType::SnapshotLevel;
            } else {
                e.type = e.qty_lots > 0 ? EventType::Set : EventType::Delete;
            }
            out.push_back(e);
        }
    }
    if (snap) {
        Event e = proto;
        e.type = EventType::SnapshotEnd;
        out.push_back(e);
    }
    out.back().flags = kLastInMsg;
    return ParseStatus::Ok;
}

}  // namespace md
