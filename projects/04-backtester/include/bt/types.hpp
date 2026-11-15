// Shared value types of the backtester (DESIGN.md section 8.3). All data is SYNTHETIC.
#pragma once

#include <cstdint>
#include <string_view>

#include "md/types.hpp"

namespace bt {

using OrderId = std::int64_t;
using md::Side;

inline Side opposite(Side s) {
    return s == Side::Bid ? Side::Ask : (s == Side::Ask ? Side::Bid : Side::None);
}

enum class QueueModel : std::uint8_t { Optimistic, FifoBack, FifoCancelAhead };
constexpr std::int64_t kPpm = 1'000'000;
struct QueueConfig {
    QueueModel model = QueueModel::FifoBack;
    std::int64_t cancel_ahead_ppm = 0;
};

enum class ReportType : std::uint8_t { Accepted, Rejected, Filled, Canceled, CancelRejected };
enum class RejectReason : std::uint8_t { None, WouldCross, BookNotLive, TooLateToCancel, UnknownOrder };
enum class FillKind : std::uint8_t { TradeAt, TradeThrough, Cross };

struct Report {
    ReportType type = ReportType::Accepted;
    OrderId id = 0;
    Side side = Side::None;
    std::int64_t px_ticks = 0;
    std::int64_t qty_lots = 0;        // Filled: fill qty; Canceled: canceled qty; else 0
    std::int64_t remaining_lots = 0;  // after this event
    std::int64_t exch_ts_ns = 0;      // when it happened at the exchange
    std::int64_t ts_ns = 0;           // when delivered to the strategy
    RejectReason reason = RejectReason::None;
    FillKind fill_kind = FillKind::TradeAt;
};

struct Fill {
    std::int64_t ts_ns = 0;
    OrderId id = 0;
    Side side = Side::None;
    std::int64_t px_ticks = 0, qty_lots = 0;
    FillKind kind = FillKind::TradeAt;
    std::int64_t queue_ahead_before = 0;
    bool cancel_in_flight = false;
};

enum class OrderStatus : std::uint8_t { Open, Filled, Canceled, Rejected };

struct OrderRecord {
    OrderId id = 0;
    Side side = Side::None;
    std::int64_t px_ticks = 0, qty_lots = 0, send_ns = 0, arrive_ns = -1;
    OrderStatus status = OrderStatus::Open;
    RejectReason reject = RejectReason::None;
    std::int64_t queue_ahead_at_arrival = -1, cancel_send_ns = -1, cancel_arrive_ns = -1,
                 filled_lots = 0, filled_in_cancel_flight_lots = 0;
};

const char* to_string(QueueModel);  // "optimistic" "fifo_back" "fifo_cancel_ahead"
bool parse_queue_model(std::string_view s, QueueModel& out);
// "0" "0.25" "1" "1.000000"; value in [0,1]; at most 6 decimals; integer arithmetic only.
bool parse_fraction_ppm(std::string_view s, std::int64_t& ppm);
const char* to_string(ReportType);
const char* to_string(RejectReason);  // "NONE" "WOULD_CROSS" "BOOK_NOT_LIVE" "TOO_LATE_TO_CANCEL" "UNKNOWN_ORDER"
const char* to_string(FillKind);      // "TRADE_AT" "TRADE_THROUGH" "CROSS"
const char* to_string(OrderStatus);   // "OPEN" "FILLED" "CANCELED" "REJECTED"

}  // namespace bt
