// L2 order book: two sorted sides, no zero-quantity levels.
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "md/types.hpp"

namespace md {

enum class ApplyResult : std::uint8_t { Inserted, Replaced, Removed, Missing };

// Storage: per side a vector sorted so the BEST level is at the back (bids ascending,
// asks descending). Touch updates are O(1) amortised; lookups are a binary search.
class Book {
public:
    void clear();
    // qty > 0 expected; qty <= 0 is treated as remove_level (never stores a zero level).
    ApplyResult set_level(Side s, std::int64_t px, std::int64_t qty);
    ApplyResult remove_level(Side s, std::int64_t px);  // Missing if absent
    void load(const std::vector<std::pair<Side, Level>>& levels);  // clear + insert all
    std::int64_t qty_at(Side s, std::int64_t px) const;  // 0 if absent
    std::size_t depth(Side s) const;
    bool empty(Side s) const;
    Level best(Side s) const;  // precondition !empty(s)
    void top_n(Side s, std::size_t n, std::vector<Level>& out) const;  // clears out, best first
    bool crossed() const;
    bool locked() const;
    bool invariants_ok() const;

    friend bool operator==(const Book& a, const Book& b);

private:
    const std::vector<Level>& side_vec(Side s) const { return s == Side::Bid ? bids_ : asks_; }
    std::vector<Level>& side_vec(Side s) { return s == Side::Bid ? bids_ : asks_; }
    std::vector<Level> bids_;  // ascending price, best (highest) at back
    std::vector<Level> asks_;  // descending price, best (lowest) at back
};

bool operator==(const Book& a, const Book& b);
inline bool operator!=(const Book& a, const Book& b) { return !(a == b); }

}  // namespace md
