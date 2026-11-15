// mdtool: command line driver (normalize, replay, merge, export, bench).
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "md/bench.hpp"
#include "md/export.hpp"
#include "md/merge.hpp"
#include "md/replay.hpp"

namespace {

constexpr int kOk = 0, kUsage = 2, kIo = 3, kParseErrors = 4, kMismatch = 5, kBenchUnstable = 6;

struct Args {
    std::map<std::string, std::string> opts;
    bool ok = true;
    std::string error;
};

// Parses "--flag value" pairs; only flags in `allowed` are accepted.
Args parse_args(int argc, char** argv, int first, const std::vector<std::string>& allowed) {
    Args a;
    for (int i = first; i < argc; ++i) {
        const std::string flag = argv[i];
        bool known = false;
        for (const auto& k : allowed)
            if (k == flag) known = true;
        if (!known) {
            a.ok = false;
            a.error = "unknown argument '" + flag + "'";
            return a;
        }
        if (i + 1 >= argc) {
            a.ok = false;
            a.error = "missing value for " + flag;
            return a;
        }
        if (a.opts.count(flag) != 0) {
            a.ok = false;
            a.error = "duplicate flag " + flag;
            return a;
        }
        a.opts[flag] = argv[++i];
    }
    return a;
}

int usage(const std::string& msg) {
    if (!msg.empty()) std::cerr << "mdtool: " << msg << '\n';
    std::cerr << "usage:\n"
                 "  mdtool normalize --venue A|B --in PATH [--out PATH]\n"
                 "  mdtool replay    --venue A|B --in PATH [--truth PATH] [--tob PATH]\n"
                 "  mdtool merge     --a PATH --b PATH [--out PATH]\n"
                 "  mdtool export    --a PATH --b PATH --out PATH [--venue merged|A|B]\n"
                 "  mdtool bench     --a PATH --b PATH [--reps N] [--warmup N]\n";
    return kUsage;
}

bool get_venue(const Args& a, md::Venue& v) {
    if (a.opts.count("--venue") == 0 || a.opts.count("--in") == 0) return false;
    return md::parse_venue(a.opts.at("--venue"), v);
}

int cmd_normalize(int argc, char** argv) {
    const Args a = parse_args(argc, argv, 2, {"--venue", "--in", "--out"});
    if (!a.ok) return usage(a.error);
    md::Venue v = md::Venue::A;
    md::Feed feed;
    if (!get_venue(a, v)) return usage("normalize needs --venue A|B and --in PATH");
    std::string err;
    if (!md::load_feed(a.opts.at("--in"), v, feed, err)) {
        std::cerr << "mdtool: " << err << '\n';
        return kIo;
    }
    std::int64_t errors = 0;
    if (a.opts.count("--out") != 0) {
        std::ofstream os(a.opts.at("--out"), std::ios::binary);
        if (!os) {
            std::cerr << "mdtool: cannot write '" << a.opts.at("--out") << "'\n";
            return kIo;
        }
        errors = md::normalize_feed(feed, os, std::cerr);
        os.flush();
        if (!os) return kIo;
    } else {
        errors = md::normalize_feed(feed, std::cout, std::cerr);
    }
    return errors > 0 ? kParseErrors : kOk;
}

int cmd_replay(int argc, char** argv) {
    const Args a = parse_args(argc, argv, 2, {"--venue", "--in", "--truth", "--tob"});
    if (!a.ok) return usage(a.error);
    md::Venue v = md::Venue::A;
    md::Feed feed;
    if (!get_venue(a, v)) return usage("replay needs --venue A|B and --in PATH");
    std::string err;
    if (!md::load_feed(a.opts.at("--in"), v, feed, err)) {
        std::cerr << "mdtool: " << err << '\n';
        return kIo;
    }
    md::TruthMap truth;
    md::ReplayOptions opt;
    opt.err = &std::cerr;
    const bool with_truth = a.opts.count("--truth") != 0;
    if (with_truth) {
        if (!md::load_truth(a.opts.at("--truth"), truth, err)) {
            std::cerr << "mdtool: " << err << '\n';
            return kIo;
        }
        opt.truth = &truth;
    }
    std::ofstream tob;
    if (a.opts.count("--tob") != 0) {
        tob.open(a.opts.at("--tob"), std::ios::binary);
        if (!tob) {
            std::cerr << "mdtool: cannot write '" << a.opts.at("--tob") << "'\n";
            return kIo;
        }
        opt.tob = &tob;
    }
    const md::ReplayResult r = md::replay_feed(feed, opt);
    std::cout << md::replay_json(v, r, with_truth) << '\n';
    if (tob.is_open()) {
        tob.flush();
        if (!tob) return kIo;
    }
    if (r.truth_mismatched > 0 || r.counters.checkpoints_mismatched > 0) return kMismatch;
    return r.counters.parse_errors > 0 ? kParseErrors : kOk;
}

// Loads feed A and/or B. An unneeded feed stays empty (single-venue export).
bool load_feeds(const Args& a, bool need_a, bool need_b, md::Feed& fa, md::Feed& fb) {
    std::string err;
    fa.venue = md::Venue::A;
    fb.venue = md::Venue::B;
    if (need_a && !md::load_feed(a.opts.at("--a"), md::Venue::A, fa, err)) {
        std::cerr << "mdtool: " << err << '\n';
        return false;
    }
    if (need_b && !md::load_feed(a.opts.at("--b"), md::Venue::B, fb, err)) {
        std::cerr << "mdtool: " << err << '\n';
        return false;
    }
    return true;
}

int merge_exit_code(const md::MergeSummary& s) {
    for (int i = 0; i < 2; ++i)
        if (s.counters[i].checkpoints_mismatched > 0) return kMismatch;
    for (int i = 0; i < 2; ++i)
        if (s.counters[i].parse_errors > 0) return kParseErrors;
    return kOk;
}

int cmd_merge(int argc, char** argv) {
    const Args a = parse_args(argc, argv, 2, {"--a", "--b", "--out"});
    if (!a.ok) return usage(a.error);
    if (a.opts.count("--a") == 0 || a.opts.count("--b") == 0) return usage("merge needs --a PATH and --b PATH");
    md::Feed fa, fb;
    if (!load_feeds(a, true, true, fa, fb)) return kIo;
    std::ofstream file;
    if (a.opts.count("--out") != 0) {
        file.open(a.opts.at("--out"), std::ios::binary);
        if (!file) {
            std::cerr << "mdtool: cannot write '" << a.opts.at("--out") << "'\n";
            return kIo;
        }
    }
    std::ostream& os = file.is_open() ? static_cast<std::ostream&>(file) : std::cout;
    md::write_merged_header(os);
    const md::MergeSummary s = md::run_merged(fa, fb, [&os](const md::MergedQuote& q) { md::write_merged_row(os, q); });
    os.flush();
    if (!os) return kIo;
    std::cerr << md::merge_summary_json(s) << '\n';
    return merge_exit_code(s);
}

int cmd_export(int argc, char** argv) {
    const Args a = parse_args(argc, argv, 2, {"--a", "--b", "--out", "--venue"});
    if (!a.ok) return usage(a.error);
    const std::string which = a.opts.count("--venue") != 0 ? a.opts.at("--venue") : "merged";
    if (which != "merged" && which != "A" && which != "B") return usage("export --venue must be merged, A or B");
    const bool need_a = which != "B", need_b = which != "A";
    if (a.opts.count("--out") == 0 || (need_a && a.opts.count("--a") == 0) ||
        (need_b && a.opts.count("--b") == 0))
        return usage("export needs --out PATH and the feed(s) for the chosen --venue (--a and/or --b)");
    md::Feed fa, fb;
    if (!load_feeds(a, need_a, need_b, fa, fb)) return kIo;
    std::ofstream file(a.opts.at("--out"), std::ios::binary);
    if (!file) {
        std::cerr << "mdtool: cannot write '" << a.opts.at("--out") << "'\n";
        return kIo;
    }
    md::QuoteExporter ex(file);
    const md::MergeSummary s = md::run_merged(fa, fb, [&ex](const md::MergedQuote& q) { ex.on_quote(q); });
    ex.finish();
    file.flush();
    if (!file) return kIo;
    std::cout << "{\"rows\":" << ex.rows() << ",\"merged_quotes\":" << s.merged_quotes
              << ",\"merged_crossed\":" << s.merged_crossed << ",\"merged_locked\":" << s.merged_locked << "}\n";
    return merge_exit_code(s);
}

bool parse_count(const std::string& s, int min, int& out) {
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (s.empty() || *end != '\0' || v < min || v > 1000000) return false;
    out = static_cast<int>(v);
    return true;
}

int cmd_bench(int argc, char** argv) {
    const Args a = parse_args(argc, argv, 2, {"--a", "--b", "--reps", "--warmup"});
    if (!a.ok) return usage(a.error);
    if (a.opts.count("--a") == 0 || a.opts.count("--b") == 0) return usage("bench needs --a PATH and --b PATH");
    md::BenchConfig cfg;
    if (a.opts.count("--reps") != 0 && !parse_count(a.opts.at("--reps"), 1, cfg.reps))
        return usage("--reps must be an integer >= 1");
    if (a.opts.count("--warmup") != 0 && !parse_count(a.opts.at("--warmup"), 0, cfg.warmup))
        return usage("--warmup must be an integer >= 0");
    md::Feed fa, fb;
    if (!load_feeds(a, true, true, fa, fb)) return kIo;
    const md::BenchResult r = md::run_bench(fa, fb, cfg);
    std::cout << md::to_json(r) << '\n';
    std::cerr << md::to_text(r);
    return r.counters_stable ? kOk : kBenchUnstable;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage("missing subcommand");
    const std::string cmd = argv[1];
    if (cmd == "normalize") return cmd_normalize(argc, argv);
    if (cmd == "replay") return cmd_replay(argc, argv);
    if (cmd == "merge") return cmd_merge(argc, argv);
    if (cmd == "export") return cmd_export(argc, argv);
    if (cmd == "bench") return cmd_bench(argc, argv);
    return usage("unknown subcommand '" + cmd + "'");
}
