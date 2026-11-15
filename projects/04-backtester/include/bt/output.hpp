// Output files and the fill hash (DESIGN.md sections 5 and 6).
#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "bt/backtest.hpp"
#include "md/types.hpp"

namespace bt {

std::uint64_t fnv1a64(std::string_view bytes);  // FNV-1a 64: offset 14695981039346656037, prime 1099511628211
std::string hex16(std::uint64_t h);             // 16 lower-case hex digits

// Applies the collapse-then-filter rule of Project 3 section 9 (as md::QuoteExporter does) and records
// the first and last ts_us of the rows that would be written.
class MarketRange {
public:
    void on_quote(const md::MergedQuote& q);
    void finish();
    bool any() const { return rows_ > 0; }
    std::int64_t first_ts_us() const { return first_; }
    std::int64_t last_ts_us() const { return last_; }
    std::int64_t rows() const { return rows_; }

private:
    void flush();
    bool have_ = false;
    md::MergedQuote pending_;
    std::int64_t pending_us_ = 0, first_ = 0, last_ = 0, rows_ = 0;
};

// Writes fills.csv (header included), dropping fills outside [first_ts_us, last_ts_us]; returns rows
// written and adds the dropped count to *outside (may be null).
std::int64_t write_fills_csv(std::ostream& os, const std::vector<Fill>& fills, double fee_bps,
                             std::int64_t first_ts_us, std::int64_t last_ts_us, std::int64_t* outside);
void write_fills_detail_csv(std::ostream& os, const std::vector<Fill>& fills);
void write_orders_csv(std::ostream& os, const std::vector<OrderRecord>& orders);
void write_summary_csv(std::ostream& os, const RunSummary& s);
// Writes market.csv, fills.csv, fills_detail.csv, orders.csv and summary.csv into dir (created if
// needed) and completes summary.fills / fills_outside_market / fills_fnv1a64 in r. false + err on I/O error.
bool write_outputs(const std::string& dir, BacktestResult& r, const BacktestConfig& cfg,
                   const std::string& market_csv, std::string& err);

}  // namespace bt
