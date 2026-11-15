// md_bench: thin main around md::run_bench (same code as `mdtool bench`).
#include <cstdlib>
#include <iostream>
#include <string>

#include "md/bench.hpp"

int main(int argc, char** argv) {
    std::string a_path, b_path;
    md::BenchConfig cfg;
    for (int i = 1; i < argc; ++i) {
        const std::string f = argv[i];
        if (i + 1 >= argc) {
            std::cerr << "md_bench: missing value for " << f << '\n';
            return 2;
        }
        const std::string v = argv[++i];
        if (f == "--a") a_path = v;
        else if (f == "--b") b_path = v;
        else if (f == "--reps") cfg.reps = std::atoi(v.c_str());
        else if (f == "--warmup") cfg.warmup = std::atoi(v.c_str());
        else {
            std::cerr << "md_bench: unknown argument '" << f << "'\n";
            return 2;
        }
    }
    if (a_path.empty() || b_path.empty() || cfg.reps < 1 || cfg.warmup < 0) {
        std::cerr << "usage: md_bench --a venueA.jsonl --b venueB.csv [--reps N>=1] [--warmup N>=0]\n";
        return 2;
    }
    md::Feed a, b;
    std::string err;
    if (!md::load_feed(a_path, md::Venue::A, a, err) || !md::load_feed(b_path, md::Venue::B, b, err)) {
        std::cerr << "md_bench: " << err << '\n';
        return 3;
    }
    const md::BenchResult r = md::run_bench(a, b, cfg);
    std::cout << md::to_json(r) << '\n';
    std::cerr << md::to_text(r);
    return r.counters_stable ? 0 : 6;
}
