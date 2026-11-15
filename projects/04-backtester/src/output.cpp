#include "bt/output.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "md/fixed.hpp"

namespace bt {

std::uint64_t fnv1a64(std::string_view bytes) {
    std::uint64_t h = 14695981039346656037ULL;
    for (char c : bytes) {
        h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        h *= 1099511628211ULL;
    }
    return h;
}

std::string hex16(std::uint64_t h) {
    static const char* kDigits = "0123456789abcdef";
    std::string s(16, '0');
    for (int i = 15; i >= 0; --i) {
        s[static_cast<std::size_t>(i)] = kDigits[h & 0xF];
        h >>= 4;
    }
    return s;
}

void MarketRange::on_quote(const md::MergedQuote& q) {
    const std::int64_t us = q.local_ts_ns / 1000;
    if (have_ && us != pending_us_) flush();
    pending_ = q;
    pending_us_ = us;
    have_ = true;
}

void MarketRange::finish() {
    if (have_) flush();
}

void MarketRange::flush() {
    have_ = false;
    const md::MergedQuote& q = pending_;
    if (q.live_venues == 0 || q.bid_venues == 0 || q.ask_venues == 0 || q.bid_px >= q.ask_px) return;
    if (rows_ == 0) first_ = pending_us_;
    last_ = pending_us_;
    ++rows_;
}

namespace {

const char* side_name(Side s) { return s == Side::Bid ? "BID" : (s == Side::Ask ? "ASK" : "NONE"); }

std::string fee_text(double fee_bps, const Fill& f) {
    // Evaluated in exactly this order, in double (section 5.2).
    const double fee = ((fee_bps * 1e-4) * (static_cast<double>(f.px_ticks) / 100.0)) *
                       (static_cast<double>(f.qty_lots) / 1000.0);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.12f", fee);  // fixed 12 decimals: stable, readable, deterministic
    return buf;
}

std::string g17(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return buf;
}

}  // namespace

std::int64_t write_fills_csv(std::ostream& os, const std::vector<Fill>& fills, double fee_bps,
                             std::int64_t first_ts_us, std::int64_t last_ts_us, std::int64_t* outside) {
    os << "ts_us,side,px,qty,fee\n";
    std::int64_t rows = 0;
    for (const Fill& f : fills) {
        const std::int64_t us = f.ts_ns / 1000;
        if (us < first_ts_us || us > last_ts_us) {
            if (outside != nullptr) ++*outside;
            continue;
        }
        os << us << ',' << (f.side == Side::Bid ? "1" : "-1") << ',' << md::format_fixed(f.px_ticks, 2) << ','
           << md::format_fixed(f.qty_lots, 3) << ',' << fee_text(fee_bps, f) << '\n';
        ++rows;
    }
    return rows;
}

void write_fills_detail_csv(std::ostream& os, const std::vector<Fill>& fills) {
    os << "ts_ns,order_id,side,px_ticks,qty_lots,kind,queue_ahead_before,cancel_in_flight\n";
    for (const Fill& f : fills) {
        os << f.ts_ns << ',' << f.id << ',' << side_name(f.side) << ',' << f.px_ticks << ',' << f.qty_lots << ','
           << to_string(f.kind) << ',' << f.queue_ahead_before << ',' << (f.cancel_in_flight ? 1 : 0) << '\n';
    }
}

void write_orders_csv(std::ostream& os, const std::vector<OrderRecord>& orders) {
    os << "order_id,side,px_ticks,qty_lots,send_ns,arrive_ns,status,reject_reason,queue_ahead_at_arrival,"
          "cancel_send_ns,cancel_arrive_ns,filled_lots,filled_in_cancel_flight_lots\n";
    for (const OrderRecord& o : orders) {
        os << o.id << ',' << side_name(o.side) << ',' << o.px_ticks << ',' << o.qty_lots << ',' << o.send_ns << ','
           << o.arrive_ns << ',' << to_string(o.status) << ',' << to_string(o.reject) << ','
           << o.queue_ahead_at_arrival << ',' << o.cancel_send_ns << ',' << o.cancel_arrive_ns << ','
           << o.filled_lots << ',' << o.filled_in_cancel_flight_lots << '\n';
    }
}

void write_summary_csv(std::ostream& os, const RunSummary& s) {
    char rate[64];
    std::snprintf(rate, sizeof(rate), "%.6f", s.fill_rate);
    auto kv = [&os](const char* k, const auto& v) { os << k << ',' << v << '\n'; };
    os << "key,value\n";
    kv("model", s.model);
    kv("cancel_ahead_ppm", s.cancel_ahead_ppm);
    kv("strategy", s.strategy);
    kv("cross_fills", s.cross_fills ? "on" : "off");
    kv("entry_latency_ns", s.entry_latency_ns);
    kv("cancel_latency_ns", s.cancel_latency_ns);
    kv("ack_latency_ns", s.ack_latency_ns);
    kv("latency_jitter_mean_ns", s.latency_jitter_mean_ns);
    kv("seed", s.seed);
    kv("fee_bps", g17(s.fee_bps));
    kv("venue", s.venue);
    kv("input_events", s.input_events);
    kv("input_messages", s.input_messages);
    kv("input_trades", s.input_trades);
    kv("session_final_state", s.session_final_state);
    kv("session_gaps", s.session_gaps);
    kv("session_resyncs", s.session_resyncs);
    kv("market_rows", s.market_rows);
    kv("market_first_ts_us", s.market_first_ts_us);
    kv("market_last_ts_us", s.market_last_ts_us);
    kv("orders_sent", s.orders_sent);
    kv("orders_accepted", s.orders_accepted);
    kv("orders_rejected_would_cross", s.orders_rejected_would_cross);
    kv("orders_rejected_not_live", s.orders_rejected_not_live);
    kv("cancels_sent", s.cancels_sent);
    kv("cancels_effective", s.cancels_effective);
    kv("cancels_too_late", s.cancels_too_late);
    kv("cancels_unknown", s.cancels_unknown);
    kv("cancels_in_flight_filled", s.cancels_in_flight_filled);
    kv("fills", s.fills);
    kv("fills_trade_at", s.fills_trade_at);
    kv("fills_trade_through", s.fills_trade_through);
    kv("fills_cross", s.fills_cross);
    kv("filled_lots", s.filled_lots);
    kv("orders_with_fill", s.orders_with_fill);
    kv("fill_rate", rate);
    kv("final_position_lots", s.final_position_lots);
    kv("max_abs_position_lots", s.max_abs_position_lots);
    kv("orders_open_at_end", s.orders_open_at_end);
    kv("trade_credit_unmatched_lots", s.trade_credit_unmatched_lots);
    kv("fills_outside_market", s.fills_outside_market);
    kv("fills_fnv1a64", hex16(s.fills_fnv1a64));
}

namespace {
bool write_file(const std::filesystem::path& p, const std::string& content, std::string& err) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) {
        err = "cannot write '" + p.string() + "'";
        return false;
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.close();
    if (!out) {
        err = "write failed for '" + p.string() + "'";
        return false;
    }
    return true;
}
}  // namespace

bool write_outputs(const std::string& dir, BacktestResult& r, const BacktestConfig& cfg,
                   const std::string& market_csv, std::string& err) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        err = "cannot create directory '" + dir + "': " + ec.message();
        return false;
    }
    std::ostringstream fills;
    std::int64_t outside = 0;
    r.summary.fills = write_fills_csv(fills, r.fills, cfg.fee_bps, r.market_first_ts_us, r.market_last_ts_us, &outside);
    r.summary.fills_outside_market = outside;
    const std::string fills_text = fills.str();
    r.summary.fills_fnv1a64 = fnv1a64(fills_text);

    std::ostringstream detail, orders, summary;
    write_fills_detail_csv(detail, r.fills);
    write_orders_csv(orders, r.orders);
    write_summary_csv(summary, r.summary);

    const fs::path d(dir);
    return write_file(d / "market.csv", market_csv, err) && write_file(d / "fills.csv", fills_text, err) &&
           write_file(d / "fills_detail.csv", detail.str(), err) && write_file(d / "orders.csv", orders.str(), err) &&
           write_file(d / "summary.csv", summary.str(), err);
}

}  // namespace bt
