// Time-ordered queue of simulated events with the fixed tie-break (DESIGN.md section 2.2).
// Market messages (rank 0) stay outside the heap; the loop merges them in front.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "bt/types.hpp"

namespace bt {

enum class SimEventKind : std::uint8_t { ExchangeIn = 1, Report = 2, Timer = 3 };  // = rank
enum class InKind : std::uint8_t { NewOrder, Cancel };

struct SimEvent {
    std::int64_t ts_ns = 0;
    SimEventKind kind = SimEventKind::Timer;
    std::uint64_t seq = 0;  // assigned by push()
    InKind in_kind = InKind::NewOrder;
    OrderId id = 0;
    Report report{};
    std::int64_t timer_id = 0;
};

// Min-heap on (ts_ns, kind, seq); push assigns seq from a global counter.
class EventQueue {
public:
    void push(SimEvent e);
    bool empty() const { return heap_.empty(); }
    const SimEvent& top() const { return heap_.front(); }
    SimEvent pop();
    std::size_t size() const { return heap_.size(); }

private:
    std::vector<SimEvent> heap_;
    std::uint64_t next_seq_ = 0;
};

}  // namespace bt
