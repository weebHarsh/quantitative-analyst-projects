#include "md/book.hpp"

#include <algorithm>

namespace md {
namespace {

// "worse" ordering: element a sorts before b when a is farther from the touch.
inline bool worse(Side s, std::int64_t a, std::int64_t b) {
    return s == Side::Bid ? a < b : a > b;
}

// First element that is not worse than px (lower_bound in storage order).
std::vector<Level>::iterator find_pos(Side s, std::vector<Level>& v, std::int64_t px) {
    return std::lower_bound(v.begin(), v.end(), px,
                            [s](const Level& l, std::int64_t p) { return worse(s, l.price_ticks, p); });
}
std::vector<Level>::const_iterator find_pos(Side s, const std::vector<Level>& v, std::int64_t px) {
    return std::lower_bound(v.begin(), v.end(), px,
                            [s](const Level& l, std::int64_t p) { return worse(s, l.price_ticks, p); });
}

}  // namespace

void Book::clear() {
    bids_.clear();
    asks_.clear();
}

ApplyResult Book::set_level(Side s, std::int64_t px, std::int64_t qty) {
    if (s == Side::None) return ApplyResult::Missing;
    if (qty <= 0) return remove_level(s, px);
    auto& v = side_vec(s);
    auto it = find_pos(s, v, px);
    if (it != v.end() && it->price_ticks == px) {
        it->qty_lots = qty;
        return ApplyResult::Replaced;
    }
    v.insert(it, Level{px, qty});
    return ApplyResult::Inserted;
}

ApplyResult Book::remove_level(Side s, std::int64_t px) {
    if (s == Side::None) return ApplyResult::Missing;
    auto& v = side_vec(s);
    auto it = find_pos(s, v, px);
    if (it == v.end() || it->price_ticks != px) return ApplyResult::Missing;
    v.erase(it);
    return ApplyResult::Removed;
}

void Book::load(const std::vector<std::pair<Side, Level>>& levels) {
    clear();
    for (const auto& pl : levels) {
        if (pl.second.qty_lots <= 0) continue;
        if (pl.first == Side::Bid) bids_.push_back(pl.second);
        else if (pl.first == Side::Ask) asks_.push_back(pl.second);
    }
    for (Side s : {Side::Bid, Side::Ask}) {
        auto& v = side_vec(s);
        // stable sort, then keep the LAST occurrence of any duplicated price (upsert semantics)
        std::stable_sort(v.begin(), v.end(), [s](const Level& a, const Level& b) {
            return worse(s, a.price_ticks, b.price_ticks);
        });
        std::vector<Level> out;
        out.reserve(v.size());
        for (const Level& l : v) {
            if (!out.empty() && out.back().price_ticks == l.price_ticks) out.back() = l;
            else out.push_back(l);
        }
        v.swap(out);
    }
}

std::int64_t Book::qty_at(Side s, std::int64_t px) const {
    if (s == Side::None) return 0;
    const auto& v = side_vec(s);
    auto it = find_pos(s, v, px);
    return (it != v.end() && it->price_ticks == px) ? it->qty_lots : 0;
}

std::size_t Book::depth(Side s) const { return s == Side::None ? 0 : side_vec(s).size(); }
bool Book::empty(Side s) const { return depth(s) == 0; }
Level Book::best(Side s) const { return side_vec(s).back(); }

void Book::top_n(Side s, std::size_t n, std::vector<Level>& out) const {
    out.clear();
    if (s == Side::None) return;
    const auto& v = side_vec(s);
    const std::size_t k = std::min(n, v.size());
    for (std::size_t i = 0; i < k; ++i) out.push_back(v[v.size() - 1 - i]);
}

bool Book::crossed() const {
    return !bids_.empty() && !asks_.empty() && bids_.back().price_ticks > asks_.back().price_ticks;
}
bool Book::locked() const {
    return !bids_.empty() && !asks_.empty() && bids_.back().price_ticks == asks_.back().price_ticks;
}

bool Book::invariants_ok() const {
    for (Side s : {Side::Bid, Side::Ask}) {
        const auto& v = side_vec(s);
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (v[i].qty_lots <= 0) return false;
            if (i > 0 && !worse(s, v[i - 1].price_ticks, v[i].price_ticks)) return false;
        }
    }
    return true;
}

bool operator==(const Book& a, const Book& b) { return a.bids_ == b.bids_ && a.asks_ == b.asks_; }

}  // namespace md
