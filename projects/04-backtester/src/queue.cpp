#include "bt/queue.hpp"

#include <algorithm>

namespace bt {

std::int64_t queue_on_arrival(const QueueConfig& c, std::int64_t displayed) {
    return c.model == QueueModel::Optimistic ? 0 : displayed;
}

TradeResult queue_on_trade(const QueueConfig& c, std::int64_t queue_ahead, std::int64_t remaining,
                           std::int64_t volume, bool through) {
    if (through || c.model == QueueModel::Optimistic) return {std::min(remaining, volume), 0};
    const std::int64_t consumed = std::min(queue_ahead, volume);  // volume ahead of us goes first
    return {std::min(remaining, volume - consumed), queue_ahead - consumed};
}

std::int64_t queue_on_depth(const QueueConfig& c, std::int64_t queue_ahead, std::int64_t displayed_before,
                            std::int64_t displayed_after, std::int64_t explained) {
    if (c.model == QueueModel::Optimistic) return 0;
    if (displayed_after >= displayed_before) return std::min(queue_ahead, displayed_after);
    const std::int64_t r = displayed_before - displayed_after;
    const std::int64_t e = std::max<std::int64_t>(0, std::min(explained, r));
    const std::int64_t u = r - e;
    const std::int64_t q1 = std::min(queue_ahead, displayed_before - e);
    const std::int64_t behind = (displayed_before - e) - q1;
    const std::int64_t ppm = c.model == QueueModel::FifoBack ? 0 : c.cancel_ahead_ppm;
    // floor(u * ppm / 1e6) without overflow: split u into whole millions and a remainder.
    const std::int64_t frac = (u / kPpm) * ppm + ((u % kPpm) * ppm) / kPpm;
    std::int64_t cut = std::max(frac, u - behind);  // proportional share, and at least the forced amount
    cut = std::max<std::int64_t>(0, std::min(cut, std::min(u, q1)));
    return q1 - cut;
}

}  // namespace bt
