// File and text I/O helpers.
#pragma once

#include <ostream>
#include <string>
#include <vector>

#include "md/session.hpp"
#include "md/stats.hpp"
#include "md/types.hpp"

namespace md {

// Strips '\n' and a trailing '\r'. Returns false when the file cannot be opened.
bool read_lines(const std::string& path, std::vector<std::string>& lines);

struct Feed {
    Venue venue = Venue::A;
    std::vector<std::string> lines;  // B: header removed
};
// B checks the header line exactly; on any failure returns false and sets err.
bool load_feed(const std::string& path, Venue v, Feed& out, std::string& err);

void write_event_csv_header(std::ostream& os);
void write_event_csv(std::ostream& os, const Event& e);

// One line, no trailing newline. See DESIGN.md section 11 for the key order.
void write_counters_json(std::ostream& os, Venue v, const VenueCounters& c, SessionState s,
                         const LatencySummary& lat);

}  // namespace md
