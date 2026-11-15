// Per-venue sequencing, gap detection and resync (DESIGN.md section 6).
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include "md/book.hpp"
#include "md/types.hpp"

namespace md {

enum class SessionState : std::uint8_t { Init, Live, Stale };
enum class StaleReason : std::uint8_t { None, Gap, Crossed, Locked };
const char* to_string(SessionState);
const char* to_string(StaleReason);

struct VenueCounters {
    std::int64_t messages = 0, events = 0, parse_errors = 0, trades = 0, snapshots = 0, gaps = 0,
                 resyncs = 0, duplicates_ignored = 0, out_of_order = 0, crossed_detected = 0,
                 locked_detected = 0, checkpoints_matched = 0, checkpoints_mismatched = 0,
                 checkpoints_skipped_stale = 0, snapshots_ignored_late = 0, snapshot_errors = 0,
                 buffered_dropped = 0, buffered_max = 0, buffer_overflows = 0,
                 add_on_existing = 0, modify_missing = 0, delete_missing = 0;
};
bool operator==(const VenueCounters& a, const VenueCounters& b);
inline bool operator!=(const VenueCounters& a, const VenueCounters& b) { return !(a == b); }

// Counter names in output order (22) and by-name lookup (extra helpers, additive to the spec).
constexpr std::size_t kNumCounters = 22;
const char* counter_name(std::size_t i);
std::int64_t counter_value(const VenueCounters& c, std::size_t i);
bool counter_by_name(const VenueCounters& c, std::string_view name, std::int64_t& out);

struct SessionConfig {
    std::size_t max_buffered_messages = 1000000;
};

class VenueSession {
public:
    explicit VenueSession(Venue v, SessionConfig cfg = {});
    bool on_event(const Event& e);  // true when e has kLastInMsg
    void note_parse_error();        // parse_errors += 1, messages += 1
    SessionState state() const { return state_; }
    StaleReason stale_reason() const { return reason_; }
    std::int64_t last_applied() const { return last_; }  // -1 in Init
    const Book& book() const { return book_; }
    const VenueCounters& counters() const { return c_; }
    TopOfBook top() const;  // zeros and live=false unless Live
    using SnapshotHook = std::function<void(const VenueSession&, std::int64_t snapshot_seq,
                                            std::int64_t begin_msg_index)>;
    void set_before_snapshot_hook(SnapshotHook h) { hook_ = std::move(h); }

private:
    enum class Decision : std::uint8_t { Apply, Buffer, Drop };
    using Msg = std::vector<Event>;
    struct Pending {
        std::int64_t seq = 0, count = 0, begin_msg_index = 0;
        std::vector<std::pair<Side, Level>> levels;
    };

    bool continuous(const Event& first) const;
    Decision classify(const Event& first);
    void apply_event(const Event& e);
    void apply_message(const Msg& m);
    void buffer_message(Msg&& m);
    void check_book();
    void snapshot_event(const Event& e);
    void on_snapshot(std::int64_t seq, const std::vector<std::pair<Side, Level>>& levels,
                     std::int64_t begin_msg_index);
    void resync(std::int64_t seq, const std::vector<std::pair<Side, Level>>& levels, bool counted);

    Venue venue_;
    SessionConfig cfg_;
    SessionState state_ = SessionState::Init;
    StaleReason reason_ = StaleReason::None;
    Book book_;
    std::int64_t last_ = -1;
    std::int64_t hi_ = -1;
    std::vector<Msg> buf_;
    std::set<std::pair<std::int64_t, std::int64_t>> buf_keys_;
    bool has_pending_ = false;
    Pending pending_;
    bool in_update_ = false;
    std::int64_t cur_msg_ = 0;
    Decision decision_ = Decision::Drop;
    Msg collect_;
    VenueCounters c_;
    SnapshotHook hook_;
};

}  // namespace md
