// Export of the merged quote stream to Project 1's market schema (DESIGN.md section 9).
#pragma once

#include <cstdint>
#include <ostream>

#include "md/types.hpp"

namespace md {

// Precondition: quotes arrive with non-decreasing local_ts_ns (run_merged guarantees it), so
// ts_us in the output is strictly increasing.
class QuoteExporter {
public:
    explicit QuoteExporter(std::ostream& os);  // writes the header
    void on_quote(const MergedQuote& q);       // section 9 collapse + filter
    void finish();                             // flushes the pending microsecond
    std::int64_t rows() const { return rows_; }

private:
    void flush_pending();
    std::ostream& os_;
    bool have_ = false;
    MergedQuote pending_;
    std::int64_t pending_us_ = 0;
    std::int64_t rows_ = 0;
};

}  // namespace md
