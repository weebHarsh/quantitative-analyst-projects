#include "md/session.hpp"

#include <algorithm>

namespace md {

const char* to_string(SessionState s) {
    switch (s) {
        case SessionState::Init: return "INIT";
        case SessionState::Live: return "LIVE";
        case SessionState::Stale: return "STALE";
    }
    return "?";
}
const char* to_string(StaleReason r) {
    switch (r) {
        case StaleReason::None: return "NONE";
        case StaleReason::Gap: return "GAP";
        case StaleReason::Crossed: return "CROSSED";
        case StaleReason::Locked: return "LOCKED";
    }
    return "?";
}

namespace {
struct CounterDef {
    const char* name;
    std::int64_t VenueCounters::*ptr;
};
constexpr CounterDef kCounters[kNumCounters] = {
    {"messages", &VenueCounters::messages},
    {"events", &VenueCounters::events},
    {"parse_errors", &VenueCounters::parse_errors},
    {"trades", &VenueCounters::trades},
    {"snapshots", &VenueCounters::snapshots},
    {"gaps", &VenueCounters::gaps},
    {"resyncs", &VenueCounters::resyncs},
    {"duplicates_ignored", &VenueCounters::duplicates_ignored},
    {"out_of_order", &VenueCounters::out_of_order},
    {"crossed_detected", &VenueCounters::crossed_detected},
    {"locked_detected", &VenueCounters::locked_detected},
    {"checkpoints_matched", &VenueCounters::checkpoints_matched},
    {"checkpoints_mismatched", &VenueCounters::checkpoints_mismatched},
    {"checkpoints_skipped_stale", &VenueCounters::checkpoints_skipped_stale},
    {"snapshots_ignored_late", &VenueCounters::snapshots_ignored_late},
    {"snapshot_errors", &VenueCounters::snapshot_errors},
    {"buffered_dropped", &VenueCounters::buffered_dropped},
    {"buffered_max", &VenueCounters::buffered_max},
    {"buffer_overflows", &VenueCounters::buffer_overflows},
    {"add_on_existing", &VenueCounters::add_on_existing},
    {"modify_missing", &VenueCounters::modify_missing},
    {"delete_missing", &VenueCounters::delete_missing},
};
}  // namespace

const char* counter_name(std::size_t i) { return i < kNumCounters ? kCounters[i].name : ""; }
std::int64_t counter_value(const VenueCounters& c, std::size_t i) {
    return i < kNumCounters ? c.*(kCounters[i].ptr) : 0;
}
bool counter_by_name(const VenueCounters& c, std::string_view name, std::int64_t& out) {
    for (const auto& d : kCounters) {
        if (name == d.name) {
            out = c.*(d.ptr);
            return true;
        }
    }
    return false;
}
bool operator==(const VenueCounters& a, const VenueCounters& b) {
    for (const auto& d : kCounters)
        if (a.*(d.ptr) != b.*(d.ptr)) return false;
    return true;
}

VenueSession::VenueSession(Venue v, SessionConfig cfg) : venue_(v), cfg_(cfg) {}

void VenueSession::note_parse_error() {
    ++c_.parse_errors;
    ++c_.messages;
}

TopOfBook VenueSession::top() const {
    TopOfBook t;
    if (state_ != SessionState::Live) return t;
    t.live = true;
    if (!book_.empty(Side::Bid)) {
        const Level l = book_.best(Side::Bid);
        t.bid_px = l.price_ticks;
        t.bid_qty = l.qty_lots;
    }
    if (!book_.empty(Side::Ask)) {
        const Level l = book_.best(Side::Ask);
        t.ask_px = l.price_ticks;
        t.ask_qty = l.qty_lots;
    }
    return t;
}

// continuous(m): A: first <= last+1 <= last_seq; B: prev_seq == last.
bool VenueSession::continuous(const Event& m) const {
    if (venue_ == Venue::A) return m.seq_first <= last_ + 1 && last_ + 1 <= m.seq_last;
    return m.prev_seq == last_;
}

VenueSession::Decision VenueSession::classify(const Event& m) {
    const std::int64_t seq = m.seq_last;
    if (last_ >= 0 && seq <= last_) {
        ++c_.duplicates_ignored;
        return Decision::Drop;
    }
    if (state_ == SessionState::Live) {
        if (continuous(m)) return Decision::Apply;
        ++c_.gaps;
        state_ = SessionState::Stale;
        reason_ = StaleReason::Gap;
        return Decision::Buffer;
    }
    // INIT or STALE
    if (buf_keys_.count({m.seq_first, m.seq_last}) != 0) {
        ++c_.duplicates_ignored;
        return Decision::Drop;
    }
    if (seq < hi_) ++c_.out_of_order;  // then buffer it
    return Decision::Buffer;
}

void VenueSession::apply_event(const Event& e) {
    switch (e.type) {
        case EventType::Add:
            if (e.qty_lots <= 0) {
                book_.remove_level(e.side, e.price_ticks);
            } else if (book_.set_level(e.side, e.price_ticks, e.qty_lots) == ApplyResult::Replaced) {
                ++c_.add_on_existing;
            }
            break;
        case EventType::Modify:
            if (e.qty_lots <= 0) {
                book_.remove_level(e.side, e.price_ticks);
            } else if (book_.set_level(e.side, e.price_ticks, e.qty_lots) == ApplyResult::Inserted) {
                ++c_.modify_missing;
            }
            break;
        case EventType::Set:
            book_.set_level(e.side, e.price_ticks, e.qty_lots);  // qty <= 0 removes
            break;
        case EventType::Delete:
            if (book_.remove_level(e.side, e.price_ticks) == ApplyResult::Missing) ++c_.delete_missing;
            break;
        default:
            break;
    }
}

void VenueSession::apply_message(const Msg& m) {
    for (const Event& e : m) apply_event(e);
}

void VenueSession::buffer_message(Msg&& m) {
    if (buf_.size() >= cfg_.max_buffered_messages) {
        buf_.clear();
        buf_keys_.clear();
        ++c_.buffer_overflows;
    }
    buf_keys_.insert({m.front().seq_first, m.front().seq_last});
    buf_.push_back(std::move(m));
    c_.buffered_max = std::max<std::int64_t>(c_.buffered_max, static_cast<std::int64_t>(buf_.size()));
}

void VenueSession::check_book() {
    if (book_.crossed()) {
        ++c_.crossed_detected;
        state_ = SessionState::Stale;
        reason_ = StaleReason::Crossed;
    } else if (book_.locked()) {
        ++c_.locked_detected;
        state_ = SessionState::Stale;
        reason_ = StaleReason::Locked;
    }
    if (state_ == SessionState::Stale) hi_ = std::max(hi_, last_);
}

bool VenueSession::on_event(const Event& e) {
    ++c_.events;
    switch (e.type) {
        case EventType::SnapshotBegin:
        case EventType::SnapshotLevel:
        case EventType::SnapshotEnd:
            in_update_ = false;
            collect_.clear();
            snapshot_event(e);
            break;
        case EventType::Trade:
            ++c_.trades;  // never buffered, never sequenced
            break;
        default: {  // Add / Modify / Set / Delete
            if (has_pending_) {
                ++c_.snapshot_errors;  // an update inside a snapshot aborts it
                has_pending_ = false;
                pending_.levels.clear();
            }
            const bool last_in_msg = (e.flags & kLastInMsg) != 0;
            if (!in_update_ || e.msg_index != cur_msg_) {
                in_update_ = true;
                cur_msg_ = e.msg_index;
                collect_.clear();
                decision_ = classify(e);
            }
            if (decision_ == Decision::Apply) {
                apply_event(e);
                if (last_in_msg) {
                    last_ = e.seq_last;
                    hi_ = std::max(hi_, last_);
                    check_book();
                }
            } else if (decision_ == Decision::Buffer) {
                collect_.push_back(e);
                if (last_in_msg) {
                    hi_ = std::max(hi_, e.seq_last);
                    buffer_message(std::move(collect_));
                    collect_.clear();
                }
            }
            if (last_in_msg) in_update_ = false;
            break;
        }
    }
    const bool last = (e.flags & kLastInMsg) != 0;
    if (last) ++c_.messages;
    return last;
}

void VenueSession::snapshot_event(const Event& e) {
    switch (e.type) {
        case EventType::SnapshotBegin:
            if (has_pending_) ++c_.snapshot_errors;
            has_pending_ = true;
            pending_.seq = e.seq_first;
            pending_.count = e.qty_lots;
            pending_.begin_msg_index = e.msg_index;
            pending_.levels.clear();
            break;
        case EventType::SnapshotLevel:
            if (!has_pending_ || e.seq_first != pending_.seq) {
                ++c_.snapshot_errors;
                has_pending_ = false;
                pending_.levels.clear();
                return;
            }
            pending_.levels.emplace_back(e.side, Level{e.price_ticks, e.qty_lots});
            break;
        case EventType::SnapshotEnd: {
            const bool had = has_pending_;
            has_pending_ = false;
            if (!had || e.seq_first != pending_.seq ||
                static_cast<std::int64_t>(pending_.levels.size()) != pending_.count) {
                ++c_.snapshot_errors;
                pending_.levels.clear();
                return;
            }
            std::vector<std::pair<Side, Level>> levels;
            levels.swap(pending_.levels);
            on_snapshot(pending_.seq, levels, pending_.begin_msg_index);
            break;
        }
        default:
            break;
    }
}

void VenueSession::on_snapshot(std::int64_t seq, const std::vector<std::pair<Side, Level>>& levels,
                               std::int64_t begin_msg_index) {
    ++c_.snapshots;
    if (hook_) hook_(*this, seq, begin_msg_index);  // book still holds the pre-snapshot state
    if (state_ == SessionState::Live) {
        if (seq < last_) {
            ++c_.snapshots_ignored_late;
            return;
        }
        if (seq == last_) {
            Book image;
            image.load(levels);
            if (book_ == image) ++c_.checkpoints_matched;
            else ++c_.checkpoints_mismatched;
            book_.load(levels);  // adopt the venue image
            check_book();
            return;
        }
        // seq > last: updates went missing silently
        ++c_.gaps;
        ++c_.checkpoints_skipped_stale;
        resync(seq, levels, true);
        return;
    }
    if (state_ == SessionState::Stale) {
        ++c_.checkpoints_skipped_stale;
        resync(seq, levels, true);
    } else {
        resync(seq, levels, false);
    }
}

void VenueSession::resync(std::int64_t seq, const std::vector<std::pair<Side, Level>>& levels,
                          bool counted) {
    book_.load(levels);
    last_ = seq;
    std::stable_sort(buf_.begin(), buf_.end(), [](const Msg& a, const Msg& b) {
        if (a.front().seq_first != b.front().seq_first) return a.front().seq_first < b.front().seq_first;
        return a.front().seq_last < b.front().seq_last;
    });
    for (std::size_t i = 0; i < buf_.size(); ++i) {
        const Msg& m = buf_[i];
        if (m.front().seq_last <= last_) {  // at or before the snapshot
            ++c_.buffered_dropped;
            continue;
        }
        if (continuous(m.front())) {
            apply_message(m);
            last_ = m.front().seq_last;
            continue;
        }
        // a hole remains after the snapshot
        ++c_.gaps;
        state_ = SessionState::Stale;
        reason_ = StaleReason::Gap;
        book_.clear();
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(i));
        buf_keys_.clear();
        for (const Msg& r : buf_) buf_keys_.insert({r.front().seq_first, r.front().seq_last});
        return;
    }
    buf_.clear();
    buf_keys_.clear();
    state_ = SessionState::Live;
    reason_ = StaleReason::None;
    hi_ = last_;
    if (counted) ++c_.resyncs;
    check_book();
}

}  // namespace md
