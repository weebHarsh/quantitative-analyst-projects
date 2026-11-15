// Venue B (SYNTHETIC) CSV parser.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "md/parser_a.hpp"  // ParseStatus
#include "md/types.hpp"

namespace md {

constexpr std::string_view kVenueBHeader = "recv_ns,kind,exch_ns,id,prev_id,action,side,px_e4,size";

// Splits on ',' (no quoting). Fills the first max_fields entries and returns the TOTAL field
// count (an empty line is one empty field).
std::size_t split_csv(std::string_view line, std::string_view* fields, std::size_t max_fields);

class ParserB {
public:
    ParseStatus parse(std::string_view line, std::int64_t msg_index, std::vector<Event>& out);
    const std::string& last_error() const { return err_; }

private:
    ParseStatus fail(const char* msg) {
        err_ = msg;
        return ParseStatus::Error;
    }
    std::string err_;
};

}  // namespace md
