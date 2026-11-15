// Venue A (SYNTHETIC) JSON-lines parser: a purpose-built flat-line parser, no JSON library.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "md/types.hpp"

namespace md {

enum class ParseStatus : std::uint8_t { Ok, Error };

class ParserA {
public:
    // Appends this line's events to out; on Error out is left exactly as it was.
    ParseStatus parse(std::string_view line, std::int64_t msg_index, std::vector<Event>& out);
    const std::string& last_error() const { return err_; }

private:
    using Entry = std::pair<std::string_view, std::string_view>;
    ParseStatus fail(const char* msg) {
        err_ = msg;
        return ParseStatus::Error;
    }
    bool parse_array(std::string_view line, std::size_t& pos, std::vector<Entry>& dst);
    ParseStatus parse_impl(std::string_view line, std::int64_t msg_index, std::vector<Event>& out);

    std::vector<Entry> bids_, asks_;
    std::string err_;
};

}  // namespace md
