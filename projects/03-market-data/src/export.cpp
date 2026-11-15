#include "md/export.hpp"

#include <string>

#include "md/fixed.hpp"

namespace md {

QuoteExporter::QuoteExporter(std::ostream& os) : os_(os) { os_ << "ts_us,bid,ask,mid\n"; }

void QuoteExporter::on_quote(const MergedQuote& q) {
    const std::int64_t us = q.local_ts_ns / 1000;
    if (have_ && us != pending_us_) flush_pending();
    pending_ = q;
    pending_us_ = us;
    have_ = true;
}

void QuoteExporter::finish() {
    if (have_) flush_pending();
}

void QuoteExporter::flush_pending() {
    have_ = false;
    const MergedQuote& q = pending_;
    if (q.live_venues == 0 || q.bid_venues == 0 || q.ask_venues == 0 || q.bid_px >= q.ask_px) return;
    // mid = (bid + ask) / 2 ticks = (bid + ask) * 5 in units of 0.001 (exact, integers only).
    os_ << pending_us_ << ',' << format_fixed(q.bid_px, 2) << ',' << format_fixed(q.ask_px, 2) << ','
        << format_fixed((q.bid_px + q.ask_px) * 5, 3) << '\n';
    ++rows_;
}

}  // namespace md
