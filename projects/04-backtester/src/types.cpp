#include "bt/types.hpp"

#include "md/fixed.hpp"

namespace bt {

const char* to_string(QueueModel m) {
    switch (m) {
        case QueueModel::Optimistic: return "optimistic";
        case QueueModel::FifoBack: return "fifo_back";
        case QueueModel::FifoCancelAhead: return "fifo_cancel_ahead";
    }
    return "?";
}

bool parse_queue_model(std::string_view s, QueueModel& out) {
    for (QueueModel m : {QueueModel::Optimistic, QueueModel::FifoBack, QueueModel::FifoCancelAhead}) {
        if (s == to_string(m)) {
            out = m;
            return true;
        }
    }
    return false;
}

bool parse_fraction_ppm(std::string_view s, std::int64_t& ppm) {
    const std::size_t dot = s.find('.');
    const std::string_view whole = s.substr(0, dot);
    std::string_view frac;
    if (dot != std::string_view::npos) {
        frac = s.substr(dot + 1);
        if (frac.empty() || frac.size() > 6) return false;
    }
    std::int64_t w = 0, f = 0;
    if (!md::parse_uint(whole, w) || w > 1) return false;
    if (!frac.empty()) {
        if (!md::parse_uint(frac, f)) return false;
        for (std::size_t i = frac.size(); i < 6; ++i) f *= 10;
    }
    const std::int64_t v = w * kPpm + f;
    if (v > kPpm) return false;
    ppm = v;
    return true;
}

const char* to_string(ReportType t) {
    switch (t) {
        case ReportType::Accepted: return "ACCEPTED";
        case ReportType::Rejected: return "REJECTED";
        case ReportType::Filled: return "FILLED";
        case ReportType::Canceled: return "CANCELED";
        case ReportType::CancelRejected: return "CANCEL_REJECTED";
    }
    return "?";
}

const char* to_string(RejectReason r) {
    switch (r) {
        case RejectReason::None: return "NONE";
        case RejectReason::WouldCross: return "WOULD_CROSS";
        case RejectReason::BookNotLive: return "BOOK_NOT_LIVE";
        case RejectReason::TooLateToCancel: return "TOO_LATE_TO_CANCEL";
        case RejectReason::UnknownOrder: return "UNKNOWN_ORDER";
    }
    return "?";
}

const char* to_string(FillKind k) {
    switch (k) {
        case FillKind::TradeAt: return "TRADE_AT";
        case FillKind::TradeThrough: return "TRADE_THROUGH";
        case FillKind::Cross: return "CROSS";
    }
    return "?";
}

const char* to_string(OrderStatus s) {
    switch (s) {
        case OrderStatus::Open: return "OPEN";
        case OrderStatus::Filled: return "FILLED";
        case OrderStatus::Canceled: return "CANCELED";
        case OrderStatus::Rejected: return "REJECTED";
    }
    return "?";
}

}  // namespace bt
