// Section 9.1 item 1: the event CSV reader and message grouping.
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "bt/event_csv.hpp"
#include "feed_builder.hpp"
#include "harness.hpp"
#include "md/io.hpp"

#ifndef BT_MD_GOLDEN_DIR
#error "BT_MD_GOLDEN_DIR must be defined by the build"
#endif

namespace {

const char* kHeader =
    "venue,type,side,price_ticks,qty_lots,exch_ts_ns,local_ts_ns,seq_first,seq_last,prev_seq,msg_index,flags\n";

std::string tmp_path(const std::string& name) {
    static int n = 0;
    return (std::filesystem::temp_directory_path() / ("bt_test_" + std::to_string(::getpid()) + "_" +
                                                      std::to_string(n++) + "_" + name))
        .string();
}

struct TmpFile {
    std::string path;
    explicit TmpFile(const std::string& content) : path(tmp_path("events.csv")) {
        std::ofstream o(path, std::ios::binary);
        o << content;
    }
    ~TmpFile() { std::remove(path.c_str()); }
};

bool same_event(const md::Event& a, const md::Event& b) {
    return a.price_ticks == b.price_ticks && a.qty_lots == b.qty_lots && a.exch_ts_ns == b.exch_ts_ns &&
           a.local_ts_ns == b.local_ts_ns && a.seq_first == b.seq_first && a.seq_last == b.seq_last &&
           a.prev_seq == b.prev_seq && a.msg_index == b.msg_index && a.venue == b.venue && a.type == b.type &&
           a.side == b.side && a.flags == b.flags;
}

bool reads(const std::string& content, std::string* err_out = nullptr) {
    TmpFile f(content);
    std::vector<md::Event> ev;
    std::string err;
    const bool ok = bt::read_event_csv(f.path, ev, err);
    if (err_out != nullptr) *err_out = err;
    return ok;
}

}  // namespace

TEST(event_csv_round_trip_every_type) {
    fb::Builder b;
    b.snapshot(fb::ms(1), {{100, 500}, {99, 300}}, {{101, 400}});
    b.set(fb::ms(2), md::Side::Bid, 100, 800);
    b.del(fb::ms(3), md::Side::Bid, 99);
    b.sell(fb::ms(4), 100, 50);
    // Venue B style events: ADD and MODIFY with a prev_seq, in their own messages.
    md::Event add = b.base(fb::ms(5), md::EventType::Add, md::Side::Ask, 102, 70, 55, 55);
    add.prev_seq = 54;
    add.flags = md::kLastInMsg;
    md::Event mod = b.base(fb::ms(6), md::EventType::Modify, md::Side::Ask, 102, 60, 56, 56);
    mod.prev_seq = 55;
    mod.flags = md::kLastInMsg;
    mod.msg_index = 99;
    add.msg_index = 98;
    b.ev.push_back(add);
    b.ev.push_back(mod);
    for (md::Event& e : b.ev) e.venue = md::Venue::A;  // single venue file

    std::ostringstream os;
    md::write_event_csv_header(os);
    for (const md::Event& e : b.ev) md::write_event_csv(os, e);
    TmpFile f(os.str());
    std::vector<md::Event> back;
    std::string err;
    CHECK(bt::read_event_csv(f.path, back, err));
    CHECK_EQ(back.size(), b.ev.size());
    bool all_same = back.size() == b.ev.size();
    for (std::size_t i = 0; all_same && i < back.size(); ++i) all_same = same_event(back[i], b.ev[i]);
    CHECK(all_same);
    std::vector<bt::Message> msgs;
    CHECK(bt::group_messages(back, msgs, err));
    CHECK_EQ(msgs.size(), static_cast<std::size_t>(6));
    CHECK(msgs[3].is_trade);
    CHECK(!msgs[2].is_trade);
    CHECK_EQ(msgs[0].end - msgs[0].begin, static_cast<std::size_t>(5));  // BEGIN, 3 levels, END
}

TEST(event_csv_golden_venue_a_groups_into_44_messages) {
    const std::string path = std::string(BT_MD_GOLDEN_DIR) + "/expected_events_A.csv";
    std::vector<md::Event> ev;
    std::string err;
    CHECK(bt::read_event_csv(path, ev, err));
    if (!err.empty()) std::cerr << err << "\n";
    std::vector<bt::Message> msgs;
    CHECK(bt::group_messages(ev, msgs, err));
    CHECK_EQ(msgs.size(), static_cast<std::size_t>(44));
    std::size_t trades = 0;
    for (const bt::Message& m : msgs) trades += m.is_trade ? 1 : 0;
    CHECK_EQ(trades, static_cast<std::size_t>(6));
    // Every event is accounted for exactly once.
    std::size_t total = 0;
    for (const bt::Message& m : msgs) total += m.end - m.begin;
    CHECK_EQ(total, ev.size());
}

TEST(event_csv_negative_cases) {
    const std::string ok_row = "A,SET,BID,100,500,10,20,5,5,-1,0,1\n";
    CHECK(reads(std::string(kHeader) + ok_row));
    std::string err;
    // header
    CHECK(!reads(std::string("venue,type\n") + ok_row, &err));
    CHECK(!reads("", &err));
    CHECK(!reads(std::string(kHeader).substr(0, 20) + "\n" + ok_row));
    // field count
    CHECK(!reads(std::string(kHeader) + "A,SET,BID,100,500,10,20,5,5,-1,0\n", &err));
    CHECK(!reads(std::string(kHeader) + "A,SET,BID,100,500,10,20,5,5,-1,0,1,9\n"));
    // enums and integers
    CHECK(!reads(std::string(kHeader) + "A,BOGUS,BID,100,500,10,20,5,5,-1,0,1\n"));
    CHECK(!reads(std::string(kHeader) + "A,SET,UP,100,500,10,20,5,5,-1,0,1\n"));
    CHECK(!reads(std::string(kHeader) + "C,SET,BID,100,500,10,20,5,5,-1,0,1\n"));
    CHECK(!reads(std::string(kHeader) + "A,SET,BID,1x0,500,10,20,5,5,-1,0,1\n"));
    CHECK(!reads(std::string(kHeader) + "A,SET,BID,-100,500,10,20,5,5,-1,0,1\n"));  // '-' only valid as -1 in seq
    CHECK(!reads(std::string(kHeader) + "A,SET,BID,100,500,10,20,-2,5,-1,0,1\n"));
    CHECK(!reads(std::string(kHeader) + "A,SET,BID,100,500,10,20,5,5,-1,0,256\n"));
    CHECK(!reads(std::string(kHeader) + "A,SET,BID,100,500,10,20,5,5,-1,0,1\r\n"));  // strict: \n only
    // a trade message mixing TRADE with another type
    CHECK(!reads(std::string(kHeader) + "A,TRADE,BID,100,5,10,20,1,1,-1,0,0\nA,SET,BID,100,500,10,20,5,5,-1,0,1\n", &err));
    CHECK(!reads(std::string(kHeader) + "A,SET,BID,100,500,10,20,5,5,-1,0,0\nA,TRADE,BID,100,5,10,20,1,1,-1,0,1\n"));
    // time goes backwards between messages
    CHECK(!reads(std::string(kHeader) + "A,SET,BID,100,500,10,20,5,5,-1,0,1\nA,SET,BID,100,500,10,19,6,6,-1,1,1\n", &err));
    // equal timestamps are fine
    CHECK(reads(std::string(kHeader) + "A,SET,BID,100,500,10,20,5,5,-1,0,1\nA,SET,BID,100,400,10,20,6,6,-1,1,1\n"));
    // two venues
    CHECK(!reads(std::string(kHeader) + "A,SET,BID,100,500,10,20,5,5,-1,0,1\nB,ADD,BID,100,500,10,21,6,6,5,1,1\n", &err));
    // msg_index or local_ts changes inside a message
    CHECK(!reads(std::string(kHeader) + "A,SET,BID,100,500,10,20,5,5,-1,0,0\nA,SET,BID,99,500,10,20,5,5,-1,1,1\n"));
    CHECK(!reads(std::string(kHeader) + "A,SET,BID,100,500,10,20,5,5,-1,0,0\nA,SET,BID,99,500,10,21,5,5,-1,0,1\n"));
    // last message not terminated
    CHECK(!reads(std::string(kHeader) + "A,SET,BID,100,500,10,20,5,5,-1,0,0\n"));
    // header only is a valid, empty input
    CHECK(reads(kHeader));
    // missing file reports "cannot open"
    std::vector<md::Event> ev;
    CHECK(!bt::read_event_csv(tmp_path("does_not_exist.csv"), ev, err));
    CHECK(err.rfind("cannot open", 0) == 0);
}

TEST(event_csv_parse_line_values) {
    md::Event e;
    std::string err;
    CHECK(bt::parse_event_csv_line("A,TRADE,ASK,9998,300,5,6,7,8,-1,9,1", e, err));
    CHECK(e.type == md::EventType::Trade);
    CHECK(e.side == md::Side::Ask);
    CHECK_EQ(e.price_ticks, 9998);
    CHECK_EQ(e.qty_lots, 300);
    CHECK_EQ(e.prev_seq, -1);
    CHECK_EQ(e.msg_index, 9);
    CHECK_EQ(static_cast<int>(e.flags), 1);
    CHECK(!bt::parse_event_csv_line("A,TRADE,ASK,9998,300", e, err));
    CHECK(!err.empty());
}
