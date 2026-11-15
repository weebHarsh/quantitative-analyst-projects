// bt_run: command line driver (DESIGN.md section 8.4).
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "bt/backtest.hpp"
#include "bt/event_csv.hpp"
#include "bt/output.hpp"
#include "bt/strategy_mm.hpp"
#include "bt/strategy_schedule.hpp"
#include "md/fixed.hpp"

namespace {

constexpr int kOk = 0, kUsage = 2, kIo = 3, kInvalid = 4, kInconsistent = 5;

int usage(const std::string& msg) {
    if (!msg.empty()) std::cerr << "bt_run: " << msg << '\n';
    std::cerr << "usage: bt_run --events PATH --out DIR\n"
                 "       [--model optimistic|fifo_back|fifo_cancel_ahead] [--cancel-ahead F]\n"
                 "       [--latency-us N] [--entry-latency-us N] [--cancel-latency-us N] [--ack-latency-us N]\n"
                 "       [--latency-jitter-us N] [--seed N] [--strategy mm|schedule]\n"
                 "       [--quote-qty-lots N] [--skew-inv-lots N] [--max-inv-lots N] [--skew-ticks N]\n"
                 "       [--schedule-period-us N] [--schedule-life-us N]\n"
                 "       [--fee-bps X] [--cross-fills on|off]\n";
    return kUsage;
}

bool get_int(const std::map<std::string, std::string>& o, const char* key, std::int64_t& out, bool& bad) {
    const auto it = o.find(key);
    if (it == o.end()) return false;
    if (!md::parse_uint(it->second, out)) {
        std::cerr << "bt_run: bad number for " << key << ": '" << it->second << "'\n";
        bad = true;
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    static const char* const kFlags[] = {
        "--events", "--out", "--model", "--cancel-ahead", "--latency-us", "--entry-latency-us",
        "--cancel-latency-us", "--ack-latency-us", "--latency-jitter-us", "--seed", "--strategy",
        "--quote-qty-lots", "--skew-inv-lots", "--max-inv-lots", "--skew-ticks", "--schedule-period-us",
        "--schedule-life-us", "--fee-bps", "--cross-fills"};
    std::map<std::string, std::string> o;
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        bool known = false;
        for (const char* k : kFlags)
            if (flag == k) known = true;
        if (!known) return usage("unknown argument '" + flag + "'");
        if (i + 1 >= argc) return usage("missing value for " + flag);
        if (o.count(flag) != 0) return usage("duplicate flag " + flag);
        o[flag] = argv[++i];
    }
    if (o.count("--events") == 0 || o.count("--out") == 0) return usage("--events and --out are required");

    bt::BacktestConfig cfg;
    bool bad = false;
    std::int64_t v = 0;

    if (o.count("--model") != 0 && !bt::parse_queue_model(o["--model"], cfg.queue.model))
        return usage("unknown model '" + o["--model"] + "'");
    if (cfg.queue.model == bt::QueueModel::FifoCancelAhead) {
        if (o.count("--cancel-ahead") == 0) return usage("--cancel-ahead is required with fifo_cancel_ahead");
        if (!bt::parse_fraction_ppm(o["--cancel-ahead"], cfg.queue.cancel_ahead_ppm))
            return usage("--cancel-ahead must be a decimal in [0, 1] with at most 6 decimals");
    } else if (o.count("--cancel-ahead") != 0) {
        return usage("--cancel-ahead is only valid with --model fifo_cancel_ahead");
    }

    constexpr std::int64_t kUs = 1000;
    if (get_int(o, "--latency-us", v, bad)) cfg.latency.entry_ns = cfg.latency.cancel_ns = cfg.latency.ack_ns = v * kUs;
    if (get_int(o, "--entry-latency-us", v, bad)) cfg.latency.entry_ns = v * kUs;
    if (get_int(o, "--cancel-latency-us", v, bad)) cfg.latency.cancel_ns = v * kUs;
    if (get_int(o, "--ack-latency-us", v, bad)) cfg.latency.ack_ns = v * kUs;
    if (get_int(o, "--latency-jitter-us", v, bad)) cfg.latency.jitter_mean_ns = v * kUs;
    if (get_int(o, "--seed", v, bad)) cfg.latency.seed = static_cast<std::uint64_t>(v);

    std::string strategy = "mm";
    if (o.count("--strategy") != 0) strategy = o["--strategy"];
    if (strategy != "mm" && strategy != "schedule") return usage("unknown strategy '" + strategy + "'");

    bt::MMParams mm;
    if (get_int(o, "--quote-qty-lots", v, bad)) mm.quote_qty_lots = v;
    if (get_int(o, "--skew-inv-lots", v, bad)) mm.skew_inv_lots = v;
    if (get_int(o, "--max-inv-lots", v, bad)) mm.max_inv_lots = v;
    if (get_int(o, "--skew-ticks", v, bad)) mm.skew_ticks = v;
    bt::ScheduleParams sp;
    if (get_int(o, "--schedule-period-us", v, bad)) sp.period_ns = v * kUs;
    if (get_int(o, "--schedule-life-us", v, bad)) sp.life_ns = v * kUs;
    if (get_int(o, "--quote-qty-lots", v, bad)) sp.qty_lots = v;
    if (bad) return usage("");
    if (mm.quote_qty_lots <= 0 || sp.qty_lots <= 0) return usage("--quote-qty-lots must be positive");

    if (o.count("--fee-bps") != 0) {
        const std::string& s = o["--fee-bps"];
        char* end = nullptr;
        cfg.fee_bps = std::strtod(s.c_str(), &end);
        if (s.empty() || end != s.c_str() + s.size() || !std::isfinite(cfg.fee_bps))
            return usage("bad number for --fee-bps: '" + s + "'");
    }
    if (o.count("--cross-fills") != 0) {
        const std::string& s = o["--cross-fills"];
        if (s == "on") cfg.cross_fills = true;
        else if (s == "off") cfg.cross_fills = false;
        else return usage("--cross-fills must be on or off");
    }

    const auto t0 = std::chrono::steady_clock::now();

    {
        std::ifstream probe(o["--events"], std::ios::binary);
        if (!probe) {
            std::cerr << "bt_run: cannot read '" << o["--events"] << "'\n";
            return kIo;
        }
    }
    std::vector<md::Event> events;
    std::string err;
    if (!bt::read_event_csv(o["--events"], events, err)) {
        std::cerr << "bt_run: invalid input: " << err << '\n';
        return kInvalid;
    }

    std::unique_ptr<bt::Strategy> strat;
    if (strategy == "mm") strat = std::make_unique<bt::MarketMaker>(mm);
    else strat = std::make_unique<bt::ScheduledQuoter>(sp);

    std::ostringstream market;
    bt::BacktestResult res;
    if (!bt::run_backtest(events, *strat, cfg, &market, res, err)) {
        std::cerr << "bt_run: invalid input: " << err << '\n';
        return kInvalid;
    }
    res.summary.strategy = strategy;
    if (!bt::write_outputs(o["--out"], res, cfg, market.str(), err)) {
        std::cerr << "bt_run: " << err << '\n';
        return kIo;
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::cerr << "bt_run: " << res.summary.fills << " fills, " << res.summary.orders_sent << " orders, hash "
              << bt::hex16(res.summary.fills_fnv1a64) << ", " << ms << " ms\n";
    if (res.summary.fills_outside_market > 0) {
        std::cerr << "bt_run: consistency failure: " << res.summary.fills_outside_market
                  << " fills outside the market range\n";
        return kInconsistent;
    }
    return kOk;
}
