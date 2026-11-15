#include "md/parser_b.hpp"

#include "md/fixed.hpp"

namespace md {

std::size_t split_csv(std::string_view line, std::string_view* fields, std::size_t max_fields) {
    std::size_t n = 0, start = 0;
    for (;;) {
        const std::size_t c = line.find(',', start);
        const std::string_view f = c == std::string_view::npos ? line.substr(start) : line.substr(start, c - start);
        if (n < max_fields) fields[n] = f;
        ++n;
        if (c == std::string_view::npos) break;
        start = c + 1;
    }
    return n;
}

ParseStatus ParserB::parse(std::string_view line, std::int64_t msg_index, std::vector<Event>& out) {
    err_.clear();
    std::string_view f[10];
    const std::size_t n = split_csv(line, f, 10);
    if (n != 9) return fail("expected 9 fields");
    enum { kRecv, kKind, kExch, kId, kPrev, kAction, kSide, kPx, kSize };

    Event e;
    e.venue = Venue::B;
    e.msg_index = msg_index;
    e.flags = kLastInMsg;
    if (!parse_uint(f[kRecv], e.local_ts_ns)) return fail("bad recv_ns");
    if (!parse_uint(f[kExch], e.exch_ts_ns)) return fail("bad exch_ns");
    std::int64_t id = 0;
    if (!parse_uint(f[kId], id)) return fail("bad id");
    e.seq_first = e.seq_last = id;

    const std::string_view kind = f[kKind];
    bool need_prev = false, need_action = false, need_side = false, need_px = false, need_size = false;
    if (kind == "SB") {
        e.type = EventType::SnapshotBegin;
        need_size = true;
    } else if (kind == "SL") {
        e.type = EventType::SnapshotLevel;
        need_side = need_px = need_size = true;
    } else if (kind == "SE") {
        e.type = EventType::SnapshotEnd;
    } else if (kind == "U") {
        need_prev = need_action = need_side = need_px = need_size = true;
    } else if (kind == "T") {
        e.type = EventType::Trade;
        need_side = need_px = need_size = true;
    } else {
        return fail("unknown kind");
    }

    // Fields the table marks empty must be empty; required ones must be present.
    if (need_prev != !f[kPrev].empty()) return fail(need_prev ? "missing prev_id" : "unexpected prev_id");
    if (need_action != !f[kAction].empty()) return fail(need_action ? "missing action" : "unexpected action");
    if (need_side != !f[kSide].empty()) return fail(need_side ? "missing side" : "unexpected side");
    if (need_px != !f[kPx].empty()) return fail(need_px ? "missing px_e4" : "unexpected px_e4");
    if (need_size != !f[kSize].empty()) return fail(need_size ? "missing size" : "unexpected size");

    if (need_prev && !parse_uint(f[kPrev], e.prev_seq)) return fail("bad prev_id");
    if (need_side) {
        if (f[kSide] == "1") e.side = Side::Bid;
        else if (f[kSide] == "2") e.side = Side::Ask;
        else return fail("bad side");
    }
    if (need_px) {
        std::int64_t px4 = 0;
        if (!parse_uint(f[kPx], px4)) return fail("bad px_e4");
        if (px4 % 100 != 0) return fail("px_e4 not divisible by 100");
        e.price_ticks = px4 / 100;
    }
    if (need_size && !parse_uint(f[kSize], e.qty_lots)) return fail("bad size");

    if (kind == "U") {
        const std::string_view a = f[kAction];
        if (a == "N") e.type = EventType::Add;
        else if (a == "C") e.type = EventType::Modify;
        else if (a == "D") e.type = EventType::Delete;
        else return fail("unknown action");
        if (e.type == EventType::Delete) {
            if (e.qty_lots != 0) return fail("delete with nonzero size");
        } else if (e.qty_lots == 0) {
            return fail("update with zero size");
        }
    } else if (kind == "SL" && e.qty_lots == 0) {
        return fail("snapshot level with zero size");
    }

    out.push_back(e);
    return ParseStatus::Ok;
}

}  // namespace md
