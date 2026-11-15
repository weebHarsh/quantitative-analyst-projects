// Outputs, summary, market.csv identity with Project 3, and end-to-end runs (sections 5, 6, 9.4 items 2-3).
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "bt/event_csv.hpp"
#include "bt/output.hpp"
#include "bt/strategy_mm.hpp"
#include "feed_builder.hpp"
#include "harness.hpp"
#include "md/export.hpp"
#include "md/io.hpp"
#include "md/merge.hpp"
#include "scripted.hpp"

#ifndef BT_MD_GOLDEN_DIR
#error "BT_MD_GOLDEN_DIR must be defined by the build"
#endif

using bt::QueueModel;
using fb::ms;
using fb::us;
using md::Side;

namespace {

std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == sep) {
            out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

// Project 3's pipeline for one venue, applied to the same events (what `mdtool export --venue A` does).
std::string md_market_from_events(const std::vector<md::Event>& ev) {
    std::vector<bt::Message> msgs;
    std::string err;
    CHECK(bt::group_messages(ev, msgs, err));
    md::VenueSession sess(md::Venue::A);
    md::Merger merger;
    std::ostringstream os;
    md::QuoteExporter ex(os);
    for (const bt::Message& m : msgs) {
        for (std::size_t k = m.begin; k < m.end; ++k) sess.on_event(ev[k]);
        if (merger.update(md::Venue::A, sess.top(), m.ts_ns)) ex.on_quote(merger.current());
    }
    ex.finish();
    return os.str();
}

std::string golden_dir() { return BT_MD_GOLDEN_DIR; }

std::string tmp_dir(const std::string& name) {
    static int n = 0;
    return (std::filesystem::temp_directory_path() /
            ("bt_test_" + std::to_string(::getpid()) + "_" + std::to_string(n++) + "_" + name))
        .string();
}

struct TmpDir {
    std::string path;
    explicit TmpDir(const std::string& name) : path(tmp_dir(name)) {}
    ~TmpDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

bt::Fill mk_fill(std::int64_t ts_ns, Side s, std::int64_t px, std::int64_t qty) {
    bt::Fill f;
    f.ts_ns = ts_ns;
    f.id = 1;
    f.side = s;
    f.px_ticks = px;
    f.qty_lots = qty;
    return f;
}

}  // namespace

TEST(output_fnv1a64_known_vectors) {
    CHECK_EQ(bt::fnv1a64(""), 0xcbf29ce484222325ULL);
    CHECK_EQ(bt::fnv1a64("a"), 0xaf63dc4c8601ec8cULL);
    CHECK_EQ(bt::fnv1a64("foobar"), 0x85944171f73967e8ULL);
    CHECK(bt::hex16(0xcbf29ce484222325ULL) == "cbf29ce484222325");
    CHECK(bt::hex16(0x1ULL) == "0000000000000001");
    CHECK_EQ(bt::hex16(0).size(), static_cast<std::size_t>(16));
}

TEST(output_fills_csv_formatting) {
    std::vector<bt::Fill> fills{mk_fill(1'700'000'000'123'456'789LL, Side::Bid, 9997, 1500),
                                mk_fill(1'700'000'000'123'456'999LL, Side::Ask, 10003, 40),
                                mk_fill(1'700'000'000'500'000'000LL, Side::Bid, 5, 1),
                                mk_fill(1'699'999'999'000'000'000LL, Side::Bid, 9997, 1000),   // before the market
                                mk_fill(1'700'000'009'000'000'000LL, Side::Ask, 9997, 1000)};  // after the market
    std::ostringstream os;
    std::int64_t outside = 0;
    const std::int64_t rows =
        bt::write_fills_csv(os, fills, 0.1, 1'700'000'000'000'000LL, 1'700'000'001'000'000LL, &outside);
    CHECK_EQ(rows, 3);
    CHECK_EQ(outside, 2);
    const std::vector<std::string> lines = split(os.str(), '\n');
    CHECK_EQ(lines.size(), static_cast<std::size_t>(5));  // header + 3 rows + trailing empty
    CHECK(lines[0] == "ts_us,side,px,qty,fee");
    CHECK(os.str().back() == '\n');
    auto expected_fee = [](double bps, std::int64_t px, std::int64_t qty) {
        const double fee = ((bps * 1e-4) * (static_cast<double>(px) / 100.0)) * (static_cast<double>(qty) / 1000.0);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.17g", fee);
        return std::string(buf);
    };
    CHECK(lines[1] == "1700000000123456,1,99.97,1.500," + expected_fee(0.1, 9997, 1500));
    CHECK(lines[2] == "1700000000123456,-1,100.03,0.040," + expected_fee(0.1, 10003, 40));
    CHECK(lines[3] == "1700000000500000,1,0.05,0.001," + expected_fee(0.1, 5, 1));
    // Project 1's loader rules: ts_us int, side +-1, qty positive, ts non-decreasing, fee parses
    std::int64_t prev = -1;
    for (std::size_t i = 1; i <= 3; ++i) {
        const std::vector<std::string> f = split(lines[i], ',');
        CHECK_EQ(f.size(), static_cast<std::size_t>(5));
        char* end = nullptr;
        const long long ts = std::strtoll(f[0].c_str(), &end, 10);
        CHECK(*end == '\0');
        CHECK(ts >= prev);
        prev = ts;
        CHECK(f[1] == "1" || f[1] == "-1");
        CHECK(std::strtod(f[3].c_str(), nullptr) > 0.0);
        CHECK(std::strtod(f[2].c_str(), nullptr) > 0.0);
        const double fee = std::strtod(f[4].c_str(), &end);
        CHECK(*end == '\0');
        CHECK(fee > 0.0);
    }
    // a different fee changes only the last column
    std::ostringstream os2;
    bt::write_fills_csv(os2, fills, -0.25, 1'700'000'000'000'000LL, 1'700'000'001'000'000LL, nullptr);
    CHECK(split(os2.str(), '\n')[1].rfind("1700000000123456,1,99.97,1.500,-", 0) == 0);
}

TEST(output_detail_orders_and_summary_layout) {
    std::ostringstream d;
    std::vector<bt::Fill> fills{mk_fill(42, Side::Ask, 101, 7)};
    fills[0].kind = bt::FillKind::Cross;
    fills[0].queue_ahead_before = 9;
    fills[0].cancel_in_flight = true;
    bt::write_fills_detail_csv(d, fills);
    CHECK(d.str() == "ts_ns,order_id,side,px_ticks,qty_lots,kind,queue_ahead_before,cancel_in_flight\n"
                     "42,1,ASK,101,7,CROSS,9,1\n");

    std::ostringstream o;
    bt::OrderRecord rec;
    rec.id = 3;
    rec.side = Side::Bid;
    rec.px_ticks = 100;
    rec.qty_lots = 50;
    rec.send_ns = 5;
    bt::write_orders_csv(o, {rec});
    CHECK(o.str() ==
          "order_id,side,px_ticks,qty_lots,send_ns,arrive_ns,status,reject_reason,queue_ahead_at_arrival,"
          "cancel_send_ns,cancel_arrive_ns,filled_lots,filled_in_cancel_flight_lots\n"
          "3,BID,100,50,5,-1,OPEN,NONE,-1,-1,-1,0,0\n");

    bt::RunSummary s;
    s.model = "fifo_cancel_ahead";
    s.cancel_ahead_ppm = 500000;
    s.strategy = "mm";
    s.fee_bps = 0.1;
    s.fill_rate = 1.0 / 3.0;
    s.fills_fnv1a64 = 0xabcdefULL;
    s.session_final_state = "live";
    std::ostringstream so;
    bt::write_summary_csv(so, s);
    const std::vector<std::string> lines = split(so.str(), '\n');
    const std::vector<std::string> keys{
        "key", "model", "cancel_ahead_ppm", "strategy", "cross_fills", "entry_latency_ns", "cancel_latency_ns",
        "ack_latency_ns", "latency_jitter_mean_ns", "seed", "fee_bps", "venue", "input_events", "input_messages",
        "input_trades", "session_final_state", "session_gaps", "session_resyncs", "market_rows",
        "market_first_ts_us", "market_last_ts_us", "orders_sent", "orders_accepted", "orders_rejected_would_cross",
        "orders_rejected_not_live", "cancels_sent", "cancels_effective", "cancels_too_late", "cancels_unknown",
        "cancels_in_flight_filled", "fills", "fills_trade_at", "fills_trade_through", "fills_cross", "filled_lots",
        "orders_with_fill", "fill_rate", "final_position_lots", "max_abs_position_lots", "orders_open_at_end",
        "trade_credit_unmatched_lots", "fills_outside_market", "fills_fnv1a64"};
    CHECK_EQ(lines.size(), keys.size() + 1);  // + trailing empty
    for (std::size_t i = 0; i < keys.size() && i < lines.size(); ++i)
        CHECK(lines[i].substr(0, lines[i].find(',')) == keys[i]);
    CHECK(lines[0] == "key,value");
    CHECK(so.str().find("\nfill_rate,0.333333\n") != std::string::npos);
    CHECK(so.str().find("\nfee_bps,0.10000000000000001\n") != std::string::npos);
    CHECK(so.str().find("\nfills_fnv1a64,0000000000abcdef\n") != std::string::npos);
    CHECK(so.str().find("\ncross_fills,on\n") != std::string::npos);
    CHECK(so.str().find("\nmodel,fifo_cancel_ahead\n") != std::string::npos);
}

TEST(market_range_matches_the_exporter_rows) {
    const std::vector<md::Event> ev = fb::random_feed(5, 400);
    std::vector<bt::Message> msgs;
    std::string err;
    CHECK(bt::group_messages(ev, msgs, err));
    md::VenueSession sess(md::Venue::A);
    md::Merger merger;
    std::ostringstream os;
    md::QuoteExporter ex(os);
    bt::MarketRange range;
    for (const bt::Message& m : msgs) {
        for (std::size_t k = m.begin; k < m.end; ++k) sess.on_event(ev[k]);
        if (merger.update(md::Venue::A, sess.top(), m.ts_ns)) {
            ex.on_quote(merger.current());
            range.on_quote(merger.current());
        }
    }
    ex.finish();
    range.finish();
    const std::vector<std::string> lines = split(os.str(), '\n');
    CHECK(ex.rows() > 10);
    CHECK_EQ(range.rows(), ex.rows());
    CHECK(range.any());
    CHECK_EQ(std::to_string(range.first_ts_us()), lines[1].substr(0, lines[1].find(',')));
    CHECK_EQ(std::to_string(range.last_ts_us()), lines[lines.size() - 2].substr(0, lines[lines.size() - 2].find(',')));
    bt::MarketRange empty;
    empty.finish();
    CHECK(!empty.any());
}

TEST(market_csv_is_identical_to_project3_export_on_a_random_feed) {
    const std::vector<md::Event> ev = fb::random_feed(11, 1500);
    bt::MarketMaker mm;
    const sc::RunOut r = sc::run(ev, mm, sc::make_cfg(QueueModel::FifoCancelAhead, 500000, ms(1)));
    CHECK(r.ok);
    CHECK(r.market == md_market_from_events(ev));
    CHECK(r.market.rfind("ts_us,bid,ask,mid\n", 0) == 0);
    CHECK_EQ(r.res.summary.market_rows + 1, static_cast<std::int64_t>(split(r.market, '\n').size()) - 1);
}

TEST(golden_venue_a_end_to_end) {
    std::vector<md::Event> ev;
    std::string err;
    CHECK(bt::read_event_csv(golden_dir() + "/expected_events_A.csv", ev, err));

    // Project 3's own export of the same raw feed (single venue = the other feed empty)
    md::Feed a, b;
    CHECK(md::load_feed(golden_dir() + "/venueA.jsonl", md::Venue::A, a, err));
    b.venue = md::Venue::B;
    std::ostringstream p3;
    md::QuoteExporter ex(p3);
    md::run_merged(a, b, [&ex](const md::MergedQuote& q) { ex.on_quote(q); });
    ex.finish();

    auto go = [&ev](std::int64_t latency_ns) {
        bt::MarketMaker mm;
        return sc::run(ev, mm, sc::make_cfg(QueueModel::FifoCancelAhead, 500000, latency_ns));
    };
    for (std::int64_t l : {std::int64_t{0}, ms(1), ms(20)}) {
        const sc::RunOut r = go(l);
        CHECK(r.ok);
        CHECK(r.market == p3.str());  // byte-identical to the Project 3 export
        CHECK_EQ(r.res.summary.input_messages, 44);
        CHECK_EQ(r.res.summary.input_events, static_cast<std::int64_t>(ev.size()));
        CHECK_EQ(r.res.summary.input_trades, 6);
        CHECK(r.res.summary.orders_sent > 0);
        CHECK_EQ(r.res.summary.fills + 0, 0);  // run_backtest leaves the write-step fields at 0
        // the second run is byte-identical
        const sc::RunOut r2 = go(l);
        CHECK(r2.fills_csv == r.fills_csv);
        CHECK_EQ(bt::fnv1a64(r2.fills_csv), bt::fnv1a64(r.fills_csv));
        // accounting identities
        std::int64_t sum = 0;
        for (const bt::Fill& f : r.res.fills) sum += f.qty_lots;
        CHECK_EQ(sum, r.res.summary.filled_lots);
        CHECK_EQ(r.res.summary.fills_trade_at + r.res.summary.fills_trade_through + r.res.summary.fills_cross,
                 static_cast<std::int64_t>(r.res.fills.size()));
    }
}

TEST(write_outputs_writes_five_consistent_files) {
    const std::vector<md::Event> ev = fb::random_feed(3, 3000);
    bt::MarketMaker mm;
    bt::BacktestConfig cfg = sc::make_cfg(QueueModel::FifoBack, 0, ms(2));
    cfg.fee_bps = 0.25;
    bt::BacktestResult res;
    std::ostringstream market;
    std::string err;
    CHECK(bt::run_backtest(ev, mm, cfg, &market, res, err));
    res.summary.strategy = "mm";
    TmpDir dir("outputs");
    CHECK(bt::write_outputs(dir.path + "/nested/run", res, cfg, market.str(), err));
    const std::string base = dir.path + "/nested/run/";
    for (const char* f : {"market.csv", "fills.csv", "fills_detail.csv", "orders.csv", "summary.csv"})
        CHECK(std::filesystem::exists(base + f));
    const std::string fills = slurp(base + "fills.csv");
    CHECK(fills.rfind("ts_us,side,px,qty,fee\n", 0) == 0);
    CHECK_EQ(res.summary.fills_fnv1a64, bt::fnv1a64(fills));
    CHECK(res.summary.fills > 20);
    CHECK_EQ(res.summary.fills, static_cast<std::int64_t>(split(fills, '\n').size()) - 2);
    CHECK_EQ(res.summary.fills_outside_market, 0);
    CHECK_EQ(res.summary.trade_credit_unmatched_lots, 0);  // venue A style: every trade is followed by its update
    const std::string summary = slurp(base + "summary.csv");
    CHECK(summary.find("fills_fnv1a64," + bt::hex16(bt::fnv1a64(fills)) + "\n") != std::string::npos);
    CHECK(summary.find("\nstrategy,mm\n") != std::string::npos);
    CHECK(summary.find("\nfee_bps,0.25\n") != std::string::npos);
    CHECK(slurp(base + "market.csv") == market.str());
    const std::string detail = slurp(base + "fills_detail.csv");
    CHECK_EQ(split(detail, '\n').size(), split(fills, '\n').size());
    const std::string orders = slurp(base + "orders.csv");
    CHECK_EQ(static_cast<std::int64_t>(split(orders, '\n').size()) - 2, res.summary.orders_sent);
    // every order row is internally consistent: filled_lots <= qty_lots and statuses match counters
    std::int64_t open = 0;
    for (const bt::OrderRecord& o : res.orders) {
        CHECK(o.filled_lots <= o.qty_lots);
        if (o.status == bt::OrderStatus::Open) ++open;
        if (o.status == bt::OrderStatus::Filled) CHECK_EQ(o.filled_lots, o.qty_lots);
    }
    CHECK_EQ(open, res.summary.orders_open_at_end);
    CHECK(std::filesystem::path(base + "x").parent_path().string() == dir.path + "/nested/run");
    // unwritable destination is reported, not thrown
    std::string err2;
    CHECK(!bt::write_outputs("/proc/definitely/not/writable", res, cfg, market.str(), err2));
    CHECK(!err2.empty());
}

TEST(run_backtest_without_market_stream_and_with_bad_input) {
    const std::vector<md::Event> ev = fb::random_feed(4, 800);
    bt::MarketMaker m1, m2;
    bt::BacktestResult a, b;
    std::string err;
    std::ostringstream mk;
    CHECK(bt::run_backtest(ev, m1, sc::make_cfg(QueueModel::FifoBack, 0, ms(1)), nullptr, a, err));
    CHECK(bt::run_backtest(ev, m2, sc::make_cfg(QueueModel::FifoBack, 0, ms(1)), &mk, b, err));
    CHECK_EQ(a.fills.size(), b.fills.size());
    CHECK_EQ(a.summary.market_rows, b.summary.market_rows);
    CHECK_EQ(a.market_first_ts_us, b.market_first_ts_us);
    CHECK(a.market_last_ts_us > a.market_first_ts_us);

    // a message that never terminates is rejected
    std::vector<md::Event> bad = ev;
    bad.back().flags = 0;
    bt::MarketMaker m3;
    bt::BacktestResult c;
    CHECK(!bt::run_backtest(bad, m3, bt::BacktestConfig{}, nullptr, c, err));
    CHECK(!err.empty());

    // empty input: a valid, empty run
    bt::MarketMaker m4;
    bt::BacktestResult e;
    std::ostringstream emk;
    CHECK(bt::run_backtest({}, m4, bt::BacktestConfig{}, &emk, e, err));
    CHECK(e.fills.empty());
    CHECK(emk.str() == "ts_us,bid,ask,mid\n");
    CHECK_EQ(e.summary.input_messages, 0);
    CHECK(e.summary.session_final_state == "init");
}

TEST(fills_in_summary_follow_the_configuration_echo) {
    const std::vector<md::Event> ev = fb::random_feed(8, 300);
    bt::MarketMaker mm;
    bt::BacktestConfig cfg = sc::make_cfg(QueueModel::FifoCancelAhead, 250000, ms(5), false);
    cfg.latency.cancel_ns = ms(2);
    cfg.latency.jitter_mean_ns = us(50);
    cfg.latency.seed = 9;
    bt::BacktestResult r;
    std::string err;
    CHECK(bt::run_backtest(ev, mm, cfg, nullptr, r, err));
    const bt::RunSummary& s = r.summary;
    CHECK(s.model == "fifo_cancel_ahead");
    CHECK_EQ(s.cancel_ahead_ppm, 250000);
    CHECK(!s.cross_fills);
    CHECK_EQ(s.entry_latency_ns, ms(5));
    CHECK_EQ(s.cancel_latency_ns, ms(2));
    CHECK_EQ(s.ack_latency_ns, ms(5));
    CHECK_EQ(s.latency_jitter_mean_ns, us(50));
    CHECK_EQ(s.seed, static_cast<std::uint64_t>(9));
    CHECK(s.venue == "A");
    // cancel_ahead_ppm is reported as 0 for the other models whatever the config holds
    bt::MarketMaker mm2;
    bt::BacktestResult r2;
    CHECK(bt::run_backtest(ev, mm2, sc::make_cfg(QueueModel::FifoBack, 777777), nullptr, r2, err));
    CHECK_EQ(r2.summary.cancel_ahead_ppm, 0);
}
