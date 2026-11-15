// The simulated exchange and the event loop (DESIGN.md sections 2 and 3).
#include "bt/backtest.hpp"

#include <algorithm>
#include <cassert>
#include <map>
#include <streambuf>
#include <utility>

#include "bt/event_csv.hpp"
#include "bt/event_queue.hpp"
#include "bt/output.hpp"
#include "bt/queue.hpp"
#include "md/export.hpp"
#include "md/merge.hpp"
#include "md/session.hpp"

namespace bt {

namespace {

class NullBuf : public std::streambuf {
public:
    int overflow(int c) override { return c; }
    std::streamsize xsputn(const char*, std::streamsize n) override { return n; }
};

enum class OState : std::uint8_t { InFlight, Live, Done };

struct SimOrder {
    OrderRecord rec;
    OState state = OState::InFlight;
    std::int64_t remaining = 0;
    std::int64_t queue_ahead = 0;
    std::int64_t cancel_window_end = -1;  // arrival time of the first cancel (known when it is sent)
    bool crossed_flag = false;
    int cancels_arrived = 0;
};

using Key = std::pair<std::size_t, std::int64_t>;  // (side index, price)

std::size_t sidx(Side s) { return s == Side::Bid ? 0 : 1; }
Side side_of(std::size_t i) { return i == 0 ? Side::Bid : Side::Ask; }

class Sim final : public StrategyContext {
public:
    Sim(const std::vector<md::Event>& ev, const std::vector<Message>& msgs, Strategy& strat,
        const BacktestConfig& cfg, std::ostream* market_out)
        : ev_(ev),
          msgs_(msgs),
          strat_(strat),
          cfg_(cfg),
          venue_(ev.empty() ? md::Venue::A : ev.front().venue),
          session_(venue_),
          lat_(cfg.latency),
          null_stream_(&null_buf_),
          exporter_(market_out != nullptr ? *market_out : null_stream_) {}

    void run() {
        std::size_t i = 0;
        const std::size_t n = msgs_.size();
        const std::int64_t last_mkt_ts = n > 0 ? msgs_.back().ts_ns : 0;
        for (;;) {
            const bool have_m = i < n;
            if (have_m && (q_.empty() || msgs_[i].ts_ns <= q_.top().ts_ns)) {
                process_market(msgs_[i]);
                ++i;
            } else if (!q_.empty() && (have_m || q_.top().ts_ns <= last_mkt_ts)) {
                SimEvent e = q_.pop();
                advance(e.ts_ns);
                dispatch(e);
            } else {
                break;
            }
        }
        exporter_.finish();
        range_.finish();
    }

    void finish(BacktestResult& out) {
        RunSummary& s = out.summary;
        s.model = to_string(cfg_.queue.model);
        s.cancel_ahead_ppm = cfg_.queue.model == QueueModel::FifoCancelAhead ? cfg_.queue.cancel_ahead_ppm : 0;
        s.cross_fills = cfg_.cross_fills;
        s.entry_latency_ns = cfg_.latency.entry_ns;
        s.cancel_latency_ns = cfg_.latency.cancel_ns;
        s.ack_latency_ns = cfg_.latency.ack_ns;
        s.latency_jitter_mean_ns = cfg_.latency.jitter_mean_ns;
        s.seed = cfg_.latency.seed;
        s.fee_bps = cfg_.fee_bps;
        s.venue = md::to_string(venue_);
        s.input_events = static_cast<std::int64_t>(ev_.size());
        s.input_messages = static_cast<std::int64_t>(msgs_.size());
        for (const md::Event& e : ev_)
            if (e.type == md::EventType::Trade) ++s.input_trades;
        switch (session_.state()) {
            case md::SessionState::Init: s.session_final_state = "init"; break;
            case md::SessionState::Live: s.session_final_state = "live"; break;
            case md::SessionState::Stale: s.session_final_state = "stale"; break;
        }
        s.session_gaps = session_.counters().gaps;
        s.session_resyncs = session_.counters().resyncs;
        s.market_rows = exporter_.rows();
        s.market_first_ts_us = range_.first_ts_us();
        s.market_last_ts_us = range_.last_ts_us();
        out.market_first_ts_us = range_.first_ts_us();
        out.market_last_ts_us = range_.last_ts_us();

        s.orders_sent = static_cast<std::int64_t>(orders_.size());
        s.orders_accepted = accepted_;
        s.orders_rejected_would_cross = rej_cross_;
        s.orders_rejected_not_live = rej_not_live_;
        s.cancels_sent = cancels_sent_;
        s.cancels_effective = cancels_effective_;
        s.cancels_too_late = cancels_too_late_;
        s.cancels_unknown = cancels_unknown_;
        s.cancels_in_flight_filled = cancels_in_flight_filled_;
        for (const Fill& f : fills_) {
            s.filled_lots += f.qty_lots;
            switch (f.kind) {
                case FillKind::TradeAt: ++s.fills_trade_at; break;
                case FillKind::TradeThrough: ++s.fills_trade_through; break;
                case FillKind::Cross: ++s.fills_cross; break;
            }
        }
        out.orders.clear();
        for (const SimOrder& o : orders_) {
            out.orders.push_back(o.rec);
            if (o.rec.filled_lots > 0) ++s.orders_with_fill;
            if (o.rec.status == OrderStatus::Open) ++s.orders_open_at_end;
        }
        s.fill_rate = s.orders_accepted > 0
                          ? static_cast<double>(s.orders_with_fill) / static_cast<double>(s.orders_accepted)
                          : 0.0;
        s.final_position_lots = position_;
        s.max_abs_position_lots = max_abs_position_;
        s.trade_credit_unmatched_lots = credit_unmatched_;
        out.fills = std::move(fills_);
    }

    // ---- StrategyContext ----
    std::int64_t now_ns() const override { return now_; }

    OrderId send_order(md::Side side, std::int64_t px_ticks, std::int64_t qty_lots) override {
        if ((side != Side::Bid && side != Side::Ask) || qty_lots <= 0) {
            assert(false && "send_order: invalid side or quantity");
            return -1;
        }
        SimOrder o;
        o.rec.id = static_cast<OrderId>(orders_.size()) + 1;
        o.rec.side = side;
        o.rec.px_ticks = px_ticks;
        o.rec.qty_lots = qty_lots;
        o.rec.send_ns = now_;
        o.remaining = qty_lots;
        orders_.push_back(o);
        SimEvent e;
        e.ts_ns = lat_.entry_arrival(now_);
        e.kind = SimEventKind::ExchangeIn;
        e.in_kind = InKind::NewOrder;
        e.id = o.rec.id;
        q_.push(e);
        return o.rec.id;
    }

    void send_cancel(OrderId id) override {
        ++cancels_sent_;
        const std::int64_t ta = lat_.cancel_arrival(now_);
        if (id >= 1 && id <= static_cast<OrderId>(orders_.size())) {
            SimOrder& o = orders_[static_cast<std::size_t>(id - 1)];
            if (o.rec.cancel_send_ns < 0) {
                o.rec.cancel_send_ns = now_;
                o.cancel_window_end = ta;
            }
        }
        SimEvent e;
        e.ts_ns = ta;
        e.kind = SimEventKind::ExchangeIn;
        e.in_kind = InKind::Cancel;
        e.id = id;
        q_.push(e);
    }

    void set_timer(std::int64_t at_ns, std::int64_t timer_id) override {
        SimEvent e;
        e.ts_ns = std::max(at_ns, now_);
        e.kind = SimEventKind::Timer;
        e.timer_id = timer_id;
        q_.push(e);
    }

private:
    SimOrder& order(OrderId id) { return orders_[static_cast<std::size_t>(id - 1)]; }

    void advance(std::int64_t ts) {
        assert(ts >= now_);
        now_ = ts;
    }

    MarketView view() const {
        MarketView v;
        v.ts_ns = now_;
        v.live = session_.state() == md::SessionState::Live;
        v.book = &session_.book();
        if (v.live) {
            const md::TopOfBook t = session_.top();
            v.has_bid = !session_.book().empty(Side::Bid);
            v.has_ask = !session_.book().empty(Side::Ask);
            v.bid_px = t.bid_px;
            v.bid_qty = t.bid_qty;
            v.ask_px = t.ask_px;
            v.ask_qty = t.ask_qty;
        }
        return v;
    }

    void dispatch(const SimEvent& e) {
        switch (e.kind) {
            case SimEventKind::ExchangeIn:
                if (e.in_kind == InKind::NewOrder) on_new_order(e.id);
                else on_cancel(e.id);
                break;
            case SimEventKind::Report:
                strat_.on_report(e.report, view(), *this);
                break;
            case SimEventKind::Timer:
                strat_.on_timer(e.timer_id, view(), *this);
                break;
        }
    }

    // Queue a report from the exchange (happening now) for delivery after the ack latency.
    void send_report(Report r) {
        r.exch_ts_ns = now_;
        r.ts_ns = lat_.report_arrival(now_);
        SimEvent e;
        e.ts_ns = r.ts_ns;
        e.kind = SimEventKind::Report;
        e.report = r;
        q_.push(e);
    }

    static Report make_report(ReportType t, const SimOrder& o) {
        Report r;
        r.type = t;
        r.id = o.rec.id;
        r.side = o.rec.side;
        r.px_ticks = o.rec.px_ticks;
        return r;
    }

    void remove_live(const SimOrder& o) {
        std::vector<OrderId>& l = live_[sidx(o.rec.side)];
        l.erase(std::remove(l.begin(), l.end(), o.rec.id), l.end());
    }

    // Better priority first: better price, then earlier arrival, then lower id (same side only).
    bool has_priority(OrderId a, OrderId b) {
        const SimOrder& x = order(a);
        const SimOrder& y = order(b);
        if (x.rec.px_ticks != y.rec.px_ticks)
            return x.rec.side == Side::Bid ? x.rec.px_ticks > y.rec.px_ticks : x.rec.px_ticks < y.rec.px_ticks;
        if (x.rec.arrive_ns != y.rec.arrive_ns) return x.rec.arrive_ns < y.rec.arrive_ns;
        return a < b;
    }

    void reject(SimOrder& o, RejectReason why) {
        o.state = OState::Done;
        o.rec.status = OrderStatus::Rejected;
        o.rec.reject = why;
        if (why == RejectReason::WouldCross) ++rej_cross_;
        else ++rej_not_live_;
        Report r = make_report(ReportType::Rejected, o);
        r.reason = why;
        send_report(r);
    }

    void on_new_order(OrderId id) {
        SimOrder& o = order(id);
        assert(o.state == OState::InFlight);
        o.rec.arrive_ns = now_;
        if (session_.state() != md::SessionState::Live) {
            reject(o, RejectReason::BookNotLive);
            return;
        }
        const md::Book& b = session_.book();
        const Side opp = opposite(o.rec.side);
        if (!b.empty(opp)) {
            const std::int64_t best = b.best(opp).price_ticks;
            const bool crosses = o.rec.side == Side::Bid ? o.rec.px_ticks >= best : o.rec.px_ticks <= best;
            if (crosses) {
                reject(o, RejectReason::WouldCross);
                return;
            }
        }
        o.state = OState::Live;
        o.queue_ahead = queue_on_arrival(cfg_.queue, b.qty_at(o.rec.side, o.rec.px_ticks));
        o.rec.queue_ahead_at_arrival = o.queue_ahead;
        o.crossed_flag = false;
        live_[sidx(o.rec.side)].push_back(id);
        ++accepted_;
        Report r = make_report(ReportType::Accepted, o);
        r.remaining_lots = o.remaining;
        send_report(r);
    }

    void on_cancel(OrderId id) {
        Report r;
        r.id = id;
        r.type = ReportType::CancelRejected;
        if (id < 1 || id > static_cast<OrderId>(orders_.size())) {
            r.reason = RejectReason::UnknownOrder;
            ++cancels_unknown_;
            send_report(r);
            return;
        }
        SimOrder& o = order(id);
        const bool first = o.cancels_arrived++ == 0;
        if (first) o.rec.cancel_arrive_ns = now_;
        r.side = o.rec.side;
        r.px_ticks = o.rec.px_ticks;
        if (o.state == OState::Live) {
            r.type = ReportType::Canceled;
            r.qty_lots = o.remaining;
            r.remaining_lots = 0;
            o.state = OState::Done;
            o.rec.status = OrderStatus::Canceled;
            remove_live(o);
            ++cancels_effective_;
        } else if (o.state == OState::Done && o.rec.status == OrderStatus::Filled) {
            r.reason = RejectReason::TooLateToCancel;
            ++cancels_too_late_;
        } else {
            assert(o.state != OState::InFlight);  // the shared FIFO channel makes this impossible
            r.reason = RejectReason::UnknownOrder;
            ++cancels_unknown_;
        }
        send_report(r);
        if (first && o.rec.filled_in_cancel_flight_lots > 0) ++cancels_in_flight_filled_;
    }

    void do_fill(SimOrder& o, std::int64_t qty, FillKind kind, std::int64_t queue_before) {
        assert(o.state == OState::Live && qty > 0 && qty <= o.remaining);
        Fill f;
        f.ts_ns = now_;
        f.id = o.rec.id;
        f.side = o.rec.side;
        f.px_ticks = o.rec.px_ticks;
        f.qty_lots = qty;
        f.kind = kind;
        f.queue_ahead_before = queue_before;
        f.cancel_in_flight = o.rec.cancel_send_ns >= 0 && o.rec.cancel_send_ns < now_ && now_ <= o.cancel_window_end;
        o.remaining -= qty;
        o.rec.filled_lots += qty;
        if (f.cancel_in_flight) o.rec.filled_in_cancel_flight_lots += qty;
        position_ += o.rec.side == Side::Bid ? qty : -qty;
        max_abs_position_ = std::max(max_abs_position_, position_ < 0 ? -position_ : position_);
        fills_.push_back(f);
        Report r = make_report(ReportType::Filled, o);
        r.qty_lots = qty;
        r.remaining_lots = o.remaining;
        r.fill_kind = kind;
        if (o.remaining == 0) {
            o.state = OState::Done;
            o.rec.status = OrderStatus::Filled;
            remove_live(o);
        }
        send_report(r);
    }

    // Section 3.3.
    void match_trade(const md::Event& e) {
        const Side s = opposite(e.side);
        if (s == Side::None) return;
        const std::vector<OrderId>& l = live_[sidx(s)];
        if (l.empty()) return;
        const std::int64_t p = e.price_ticks;
        std::vector<OrderId> cand;
        for (OrderId id : l) {
            const std::int64_t px = order(id).rec.px_ticks;
            const bool at = px == p;
            const bool through = s == Side::Bid ? px > p : px < p;
            if (at || through) cand.push_back(id);
        }
        std::sort(cand.begin(), cand.end(), [this](OrderId a, OrderId b) { return has_priority(a, b); });
        std::int64_t volume = e.qty_lots;
        for (OrderId id : cand) {
            if (volume <= 0) break;
            SimOrder& o = order(id);
            const bool through = s == Side::Bid ? o.rec.px_ticks > p : o.rec.px_ticks < p;
            const TradeResult r = queue_on_trade(cfg_.queue, o.queue_ahead, o.remaining, volume, through);
            const std::int64_t q_before = o.queue_ahead;
            o.queue_ahead = r.queue_after;
            if (r.fill > 0) {
                do_fill(o, r.fill, through ? FillKind::TradeThrough : FillKind::TradeAt, q_before);
                volume -= r.fill;
            }
        }
    }

    // Section 3.5, edge-triggered.
    void cross_check() {
        const md::Book& b = session_.book();
        for (std::size_t si = 0; si < 2; ++si) {
            const Side side = side_of(si);
            const Side opp = opposite(side);
            const std::vector<OrderId> ids = live_[si];  // copy: fills remove orders
            std::vector<OrderId> newly;
            for (OrderId id : ids) {
                SimOrder& o = order(id);
                bool crossing = false;
                if (!b.empty(opp)) {
                    const std::int64_t best = b.best(opp).price_ticks;
                    crossing = side == Side::Bid ? best <= o.rec.px_ticks : best >= o.rec.px_ticks;
                }
                if (crossing && !o.crossed_flag) newly.push_back(id);
                o.crossed_flag = crossing;
            }
            if (newly.empty()) continue;
            std::sort(newly.begin(), newly.end(), [this](OrderId a, OrderId c) { return has_priority(a, c); });
            std::vector<md::Level> pool;  // opposite side, best first; crossing levels are a prefix
            b.top_n(opp, b.depth(opp), pool);
            for (OrderId id : newly) {
                SimOrder& o = order(id);
                auto crosses = [&](const md::Level& l) {
                    return side == Side::Bid ? l.price_ticks <= o.rec.px_ticks : l.price_ticks >= o.rec.px_ticks;
                };
                std::int64_t avail = 0;
                for (const md::Level& l : pool) {
                    if (!crosses(l)) break;
                    avail += l.qty_lots;
                }
                const std::int64_t fill = std::min(o.remaining, avail);
                const std::int64_t q_before = o.queue_ahead;
                o.queue_ahead = 0;
                if (fill <= 0) continue;
                std::int64_t left = fill;
                for (md::Level& l : pool) {
                    if (left == 0 || !crosses(l)) break;
                    const std::int64_t take = std::min(left, l.qty_lots);
                    l.qty_lots -= take;
                    left -= take;
                }
                do_fill(o, fill, FillKind::Cross, q_before);
            }
        }
    }

    void process_market(const Message& m) {
        advance(m.ts_ns);
        const bool was_live = session_.state() == md::SessionState::Live;
        if (m.is_trade) {
            for (std::size_t k = m.begin; k < m.end; ++k) {
                const md::Event& e = ev_[k];
                session_.on_event(e);
                const Side passive = opposite(e.side);
                if (passive == Side::None) continue;
                credit_[Key{sidx(passive), e.price_ticks}] += e.qty_lots;
                match_trade(e);
            }
        } else {
            std::vector<Key> keys;
            for (std::size_t si = 0; si < 2; ++si)
                for (OrderId id : live_[si]) keys.push_back(Key{si, order(id).rec.px_ticks});
            for (const auto& c : credit_) keys.push_back(c.first);
            std::sort(keys.begin(), keys.end());
            keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
            std::vector<std::int64_t> before(keys.size()), after(keys.size());
            for (std::size_t k = 0; k < keys.size(); ++k)
                before[k] = session_.book().qty_at(side_of(keys[k].first), keys[k].second);
            for (std::size_t k = m.begin; k < m.end; ++k) session_.on_event(ev_[k]);
            for (std::size_t k = 0; k < keys.size(); ++k)
                after[k] = session_.book().qty_at(side_of(keys[k].first), keys[k].second);
            const bool still_live = session_.state() == md::SessionState::Live;
            auto explained_at = [&](std::size_t k) {
                const auto it = credit_.find(keys[k]);
                const std::int64_t c = it == credit_.end() ? 0 : it->second;
                return std::min(c, std::max<std::int64_t>(before[k] - after[k], 0));
            };
            for (std::size_t si = 0; si < 2; ++si) {
                for (OrderId id : live_[si]) {
                    SimOrder& o = order(id);
                    const auto pos = std::lower_bound(keys.begin(), keys.end(), Key{si, o.rec.px_ticks});
                    const std::size_t k = static_cast<std::size_t>(pos - keys.begin());
                    if (was_live && still_live) {
                        o.queue_ahead = queue_on_depth(cfg_.queue, o.queue_ahead, before[k], after[k], explained_at(k));
                    } else {
                        o.queue_ahead = std::min(o.queue_ahead, after[k]);
                    }
                }
            }
            for (const auto& c : credit_) {
                const auto pos = std::lower_bound(keys.begin(), keys.end(), c.first);
                const std::size_t k = static_cast<std::size_t>(pos - keys.begin());
                credit_unmatched_ += c.second - explained_at(k);
            }
            credit_.clear();
            if (cfg_.cross_fills && still_live) cross_check();
        }
        if (merger_.update(venue_, session_.top(), now_)) {
            exporter_.on_quote(merger_.current());
            range_.on_quote(merger_.current());
        }
        strat_.on_market(view(), *this);
    }

    const std::vector<md::Event>& ev_;
    const std::vector<Message>& msgs_;
    Strategy& strat_;
    const BacktestConfig& cfg_;
    md::Venue venue_;
    md::VenueSession session_;
    md::Merger merger_;
    LatencyModel lat_;
    EventQueue q_;
    NullBuf null_buf_;
    std::ostream null_stream_;
    md::QuoteExporter exporter_;
    MarketRange range_;

    std::int64_t now_ = 0;
    std::vector<SimOrder> orders_;
    std::vector<OrderId> live_[2];  // ascending id (arrival order) per side
    std::map<Key, std::int64_t> credit_;
    std::vector<Fill> fills_;
    std::int64_t position_ = 0, max_abs_position_ = 0;
    std::int64_t accepted_ = 0, rej_cross_ = 0, rej_not_live_ = 0, cancels_sent_ = 0, cancels_effective_ = 0,
                 cancels_too_late_ = 0, cancels_unknown_ = 0, cancels_in_flight_filled_ = 0,
                 credit_unmatched_ = 0;
};

}  // namespace

bool run_backtest(const std::vector<md::Event>& events, Strategy& strategy, const BacktestConfig& cfg,
                  std::ostream* market_out, BacktestResult& out, std::string& err) {
    out = BacktestResult{};
    std::vector<Message> msgs;
    if (!group_messages(events, msgs, err)) return false;
    Sim sim(events, msgs, strategy, cfg, market_out);
    sim.run();
    sim.finish(out);
    return true;
}

}  // namespace bt
