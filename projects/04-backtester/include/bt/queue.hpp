// Queue-position models as pure functions in integer lots (DESIGN.md section 3).
#pragma once

#include <cstdint>

#include "bt/types.hpp"

namespace bt {

// Market quantity ahead of a new order at a level showing `displayed` lots.
std::int64_t queue_on_arrival(const QueueConfig& c, std::int64_t displayed);

struct TradeResult {
    std::int64_t fill = 0;
    std::int64_t queue_after = 0;
};
// A trade of `volume` lots reaches an order with `queue_ahead` market lots in front and `remaining`
// lots left. `through` = the trade printed at a worse price than the order's limit.
TradeResult queue_on_trade(const QueueConfig& c, std::int64_t queue_ahead, std::int64_t remaining,
                           std::int64_t volume, bool through);

// Displayed size at our level goes `displayed_before` -> `displayed_after` in one non-trade message,
// of which `explained` lots are attributed to trades. Returns the new queue_ahead (<= displayed_after).
std::int64_t queue_on_depth(const QueueConfig& c, std::int64_t queue_ahead, std::int64_t displayed_before,
                            std::int64_t displayed_after, std::int64_t explained);

}  // namespace bt
