#include "bt/event_queue.hpp"

#include <algorithm>

namespace bt {

namespace {
// "a is later than b": makes std::*_heap a min-heap.
bool later(const SimEvent& a, const SimEvent& b) {
    if (a.ts_ns != b.ts_ns) return a.ts_ns > b.ts_ns;
    if (a.kind != b.kind) return a.kind > b.kind;
    return a.seq > b.seq;
}
}  // namespace

void EventQueue::push(SimEvent e) {
    e.seq = next_seq_++;
    heap_.push_back(std::move(e));
    std::push_heap(heap_.begin(), heap_.end(), later);
}

SimEvent EventQueue::pop() {
    std::pop_heap(heap_.begin(), heap_.end(), later);
    SimEvent e = std::move(heap_.back());
    heap_.pop_back();
    return e;
}

}  // namespace bt
