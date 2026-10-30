// Reference kernel: PnL attribution and markouts in O(n + m) with two pointers.
//
//   markout_kernel fills.csv market.csv [horizon_us ...]
//
// fills.csv  : ts_us,side,px,qty,fee   (sorted by ts_us)
// market.csv : ts_us,mid               (strictly increasing ts_us)
//
// Output is "name,value" lines; markout lines are "markout,<h_us>,<n>,<qty>,<total>".
// Semantics match src/pta/pnl.py and src/pta/markouts.py: the mid is the last quote
// at or before the timestamp, and a lookup past the last quote is dropped.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

struct Fill { long long ts; int side; double px, qty, fee; };
struct Quote { long long ts; double mid; };

static std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, ',')) out.push_back(cell);
    return out;
}

static bool read_fills(const char* path, std::vector<Fill>& v) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    std::getline(in, line);  // header
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        auto c = split(line);
        if (c.size() < 5) return false;
        v.push_back({std::stoll(c[0]), std::stoi(c[1]), std::stod(c[2]), std::stod(c[3]), std::stod(c[4])});
    }
    return true;
}

static bool read_market(const char* path, std::vector<Quote>& v) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    std::getline(in, line);  // header
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        auto c = split(line);
        if (c.size() < 2) return false;
        v.push_back({std::stoll(c[0]), std::stod(c[1])});
    }
    return true;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: " << argv[0] << " fills.csv market.csv [horizon_us ...]\n";
        return 2;
    }
    std::vector<Fill> fills;
    std::vector<Quote> mk;
    if (!read_fills(argv[1], fills) || !read_market(argv[2], mk) || mk.size() < 2) {
        std::cerr << "could not read inputs\n";
        return 2;
    }
    std::vector<long long> horizons;
    for (int i = 3; i < argc; ++i) horizons.push_back(std::stoll(argv[i]));

    const double final_mid = mk.back().mid;
    const long long last_ts = mk.back().ts;

    double spread = 0.0, inventory = 0.0, fees = 0.0;
    size_t j = 0;
    for (const Fill& f : fills) {
        if (f.ts < mk.front().ts || f.ts > last_ts) {
            std::cerr << "fill outside market range at ts " << f.ts << "\n";
            return 2;
        }
        while (j + 1 < mk.size() && mk[j + 1].ts <= f.ts) ++j;
        const double m = mk[j].mid;
        spread += f.side * f.qty * (m - f.px);
        inventory += f.side * f.qty * (final_mid - m);
        fees += f.fee;
    }
    std::printf("spread_capture,%.17g\n", spread);
    std::printf("inventory,%.17g\n", inventory);
    std::printf("fees,%.17g\n", fees);
    std::printf("total,%.17g\n", spread + inventory - fees);

    for (long long h : horizons) {
        long long n = 0;
        double qty = 0.0, total = 0.0;
        size_t k = 0;
        for (const Fill& f : fills) {
            const long long t = f.ts + h;
            if (t > last_ts) break;  // fills are sorted, so the rest are past the end too
            while (k + 1 < mk.size() && mk[k + 1].ts <= t) ++k;
            const double mo = f.side * (mk[k].mid - f.px);
            ++n;
            qty += f.qty;
            total += f.qty * mo;
        }
        std::printf("markout,%lld,%lld,%.17g,%.17g\n", h, n, qty, total);
    }
    return 0;
}
