// ScriptedStrategy: timed send/cancel actions, records every report with its delivery time, plus a small
// helper that runs a backtest and renders the output files in memory.
#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "bt/backtest.hpp"
#include "bt/output.hpp"
#include "bt/strategy.hpp"

namespace sc {

struct Action {
    enum Kind { Send, Cancel } kind = Send;
    std::int64_t at_ns = 0;
    md::Side side = md::Side::Bid;
    std::int64_t px = 0, qty = 0;
    int order_ix = 0;  // Cancel: index (0-based) into the orders this script has sent
};
inline Action send(std::int64_t at, md::Side s, std::int64_t px, std::int64_t qty) {
    Action a;
    a.kind = Action::Send;
    a.at_ns = at;
    a.side = s;
    a.px = px;
    a.qty = qty;
    return a;
}
inline Action cancel(std::int64_t at, int ix) {
    Action a;
    a.kind = Action::Cancel;
    a.at_ns = at;
    a.order_ix = ix;
    return a;
}

class ScriptedStrategy : public bt::Strategy {
public:
    explicit ScriptedStrategy(std::vector<Action> a) : actions_(std::move(a)) {}

    std::vector<bt::OrderId> ids;            // ids returned by send_order, in script order
    std::vector<bt::Report> reports;         // every report as delivered (ts_ns = delivery time)
    std::vector<std::pair<std::int64_t, bool>> live_log;  // (ts, view.live) per on_market call

    void on_market(const bt::MarketView& v, bt::StrategyContext& ctx) override {
        live_log.emplace_back(v.ts_ns, v.live);
        if (started_) return;
        started_ = true;
        for (std::size_t i = 0; i < actions_.size(); ++i) ctx.set_timer(actions_[i].at_ns, static_cast<std::int64_t>(i));
    }
    void on_report(const bt::Report& r, const bt::MarketView&, bt::StrategyContext&) override { reports.push_back(r); }
    void on_timer(std::int64_t timer_id, const bt::MarketView&, bt::StrategyContext& ctx) override {
        const Action& a = actions_[static_cast<std::size_t>(timer_id)];
        if (a.kind == Action::Send) {
            ids.push_back(ctx.send_order(a.side, a.px, a.qty));
        } else {
            ctx.send_cancel(ids.at(static_cast<std::size_t>(a.order_ix)));
        }
    }

    std::vector<bt::Report> reports_of(bt::ReportType t) const {
        std::vector<bt::Report> out;
        for (const bt::Report& r : reports)
            if (r.type == t) out.push_back(r);
        return out;
    }

private:
    std::vector<Action> actions_;
    bool started_ = false;
};

struct RunOut {
    bool ok = false;
    std::string err;
    bt::BacktestResult res;
    std::string market;     // market.csv text
    std::string fills_csv;  // fills.csv text
};

inline bt::BacktestConfig make_cfg(bt::QueueModel m, std::int64_t ppm = 0, std::int64_t latency_ns = 0,
                                   bool cross = true) {
    bt::BacktestConfig c;
    c.queue.model = m;
    c.queue.cancel_ahead_ppm = ppm;
    c.latency.entry_ns = c.latency.cancel_ns = c.latency.ack_ns = latency_ns;
    c.cross_fills = cross;
    return c;
}

inline RunOut run(const std::vector<md::Event>& ev, bt::Strategy& s, const bt::BacktestConfig& cfg) {
    RunOut o;
    std::ostringstream mk;
    o.ok = bt::run_backtest(ev, s, cfg, &mk, o.res, o.err);
    o.market = mk.str();
    std::ostringstream fl;
    std::int64_t outside = 0;
    bt::write_fills_csv(fl, o.res.fills, cfg.fee_bps, o.res.market_first_ts_us, o.res.market_last_ts_us, &outside);
    o.fills_csv = fl.str();
    return o;
}

}  // namespace sc
