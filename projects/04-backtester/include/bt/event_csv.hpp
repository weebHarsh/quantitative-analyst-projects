// Reader for Project 3's normalised-event CSV (`mdtool normalize` output), DESIGN.md section 1.1.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "md/types.hpp"

namespace bt {

// Strict reader. On failure returns false and sets err (a missing file starts with "cannot open").
// Also runs group_messages, so a file that is read successfully is a valid backtest input.
bool read_event_csv(const std::string& path, std::vector<md::Event>& out, std::string& err);
// One data row (no header, no line terminator).
bool parse_event_csv_line(std::string_view line, md::Event& out, std::string& err);

struct Message {
    std::size_t begin = 0, end = 0;  // events [begin, end)
    std::int64_t ts_ns = 0;
    bool is_trade = false;
};
// Groups events into messages and validates the section 1.1 rules (single venue, one msg_index and
// one local_ts_ns per message, trade messages hold only trades, a message ends with kLastInMsg,
// time never goes backwards). false + err on violation.
bool group_messages(const std::vector<md::Event>& ev, std::vector<Message>& out, std::string& err);

}  // namespace bt
