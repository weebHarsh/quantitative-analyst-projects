// mdtool: command line driver. Pass 1: normalize and replay. merge/export/bench are stubs.
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "md/replay.hpp"

namespace {

constexpr int kOk = 0, kUsage = 2, kIo = 3, kParseErrors = 4, kMismatch = 5;

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
                 "  mdtool merge     --a PATH --b PATH [--out PATH]            (not implemented yet)\n"
                 "  mdtool export    --a PATH --b PATH --out PATH [--venue merged|A|B]  (not implemented yet)\n"
                 "  mdtool bench     --a PATH --b PATH [--reps N] [--warmup N]  (not implemented yet)\n";
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

int not_implemented(const char* name) {
    std::cerr << "mdtool: '" << name << "' is not implemented yet\n";
    return kUsage;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage("missing subcommand");
    const std::string cmd = argv[1];
    if (cmd == "normalize") return cmd_normalize(argc, argv);
    if (cmd == "replay") return cmd_replay(argc, argv);
    if (cmd == "merge" || cmd == "export" || cmd == "bench") return not_implemented(argv[1]);
    return usage("unknown subcommand '" + cmd + "'");
}
