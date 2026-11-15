#include <string>
#include <vector>

#include "harness.hpp"
#include "md/io.hpp"
#include "md/parser_a.hpp"
#include "md/parser_b.hpp"

using namespace md;

namespace {
std::string csv_of(const Event& e) {
    std::ostringstream os;
    write_event_csv(os, e);
    std::string s = os.str();
    s.pop_back();
    return s;
}
bool a_fails(const std::string& line) {
    ParserA p;
    std::vector<Event> out(2);  // pre-existing content must survive an error
    const ParseStatus st = p.parse(line, 0, out);
    return st == ParseStatus::Error && out.size() == 2 && !p.last_error().empty();
}
bool b_fails(const std::string& line) {
    ParserB p;
    std::vector<Event> out(2);
    const ParseStatus st = p.parse(line, 0, out);
    return st == ParseStatus::Error && out.size() == 2 && !p.last_error().empty();
}
std::vector<Event> a_ok(const std::string& line, std::int64_t idx = 0) {
    ParserA p;
    std::vector<Event> out;
    CHECK(p.parse(line, idx, out) == ParseStatus::Ok);
    return out;
}
std::vector<Event> b_ok(const std::string& line, std::int64_t idx = 0) {
    ParserB p;
    std::vector<Event> out;
    CHECK(p.parse(line, idx, out) == ParseStatus::Ok);
    return out;
}
}  // namespace

TEST(parser_a_snapshot) {
    const auto ev = a_ok(
        "{\"r\":1700000000000487848,\"e\":\"snapshot\",\"E\":1700000000000000,\"seq\":1000000,"
        "\"b\":[[\"100.00\",\"5.000\"],[\"99.99\",\"3.100\"]],\"a\":[[\"100.01\",\"2.100\"]]}",
        7);
    CHECK_EQ(ev.size(), static_cast<std::size_t>(5));
    CHECK_EQ(csv_of(ev[0]), std::string("A,SNAPSHOT_BEGIN,NONE,0,3,1700000000000000000,1700000000000487848,1000000,1000000,-1,7,0"));
    CHECK_EQ(csv_of(ev[1]), std::string("A,SNAPSHOT_LEVEL,BID,10000,5000,1700000000000000000,1700000000000487848,1000000,1000000,-1,7,0"));
    CHECK_EQ(csv_of(ev[2]), std::string("A,SNAPSHOT_LEVEL,BID,9999,3100,1700000000000000000,1700000000000487848,1000000,1000000,-1,7,0"));
    CHECK_EQ(csv_of(ev[3]), std::string("A,SNAPSHOT_LEVEL,ASK,10001,2100,1700000000000000000,1700000000000487848,1000000,1000000,-1,7,0"));
    CHECK_EQ(csv_of(ev[4]), std::string("A,SNAPSHOT_END,NONE,0,0,1700000000000000000,1700000000000487848,1000000,1000000,-1,7,1"));
    // empty book sides are legal
    const auto e2 = a_ok("{\"r\":1,\"e\":\"snapshot\",\"E\":2,\"seq\":3,\"b\":[],\"a\":[]}");
    CHECK_EQ(e2.size(), static_cast<std::size_t>(2));
    CHECK_EQ(e2[0].qty_lots, 0);
    CHECK((e2[1].flags & kLastInMsg) != 0);
}

TEST(parser_a_depth) {
    const auto ev = a_ok(
        "{\"r\":1700000000048691657,\"e\":\"depth\",\"E\":1700000000047972,\"first_seq\":1000001,"
        "\"last_seq\":1000003,\"b\":[[\"99.99\",\"0.000\"]],\"a\":[[\"100.01\",\"1.600\"]]}",
        3);
    CHECK_EQ(ev.size(), static_cast<std::size_t>(2));
    CHECK_EQ(csv_of(ev[0]), std::string("A,DELETE,BID,9999,0,1700000000047972000,1700000000048691657,1000001,1000003,-1,3,0"));
    CHECK_EQ(csv_of(ev[1]), std::string("A,SET,ASK,10001,1600,1700000000047972000,1700000000048691657,1000001,1000003,-1,3,1"));
    CHECK(ev[0].type == EventType::Delete);
    CHECK(ev[1].type == EventType::Set);
}

TEST(parser_a_trade) {
    const auto buy = a_ok("{\"r\":5,\"e\":\"trade\",\"E\":6,\"tid\":1,\"px\":\"100.01\",\"qty\":\"0.500\",\"side\":\"buy\"}", 2);
    CHECK_EQ(buy.size(), static_cast<std::size_t>(1));
    CHECK_EQ(csv_of(buy[0]), std::string("A,TRADE,BID,10001,500,6000,5,1,1,-1,2,1"));
    const auto sell = a_ok("{\"r\":5,\"e\":\"trade\",\"E\":6,\"tid\":9,\"px\":\"99.99\",\"qty\":\"1.250\",\"side\":\"sell\"}");
    CHECK(sell[0].side == Side::Ask);
    CHECK_EQ(sell[0].qty_lots, 1250);
}

TEST(parser_a_key_order_is_free_but_content_strict) {
    // Keys are located by name, so a permuted line is accepted.
    const auto ev = a_ok("{\"e\":\"trade\",\"side\":\"buy\",\"qty\":\"0.500\",\"px\":\"100.01\",\"tid\":1,\"E\":6,\"r\":5}");
    CHECK_EQ(ev.size(), static_cast<std::size_t>(1));
    CHECK_EQ(ev[0].local_ts_ns, 5);
}

TEST(parser_a_errors) {
    const std::string trade = "{\"r\":5,\"e\":\"trade\",\"E\":6,\"tid\":1,\"px\":\"100.01\",\"qty\":\"0.500\",\"side\":\"buy\"}";
    CHECK(!a_fails(trade));
    CHECK(a_fails(""));
    CHECK(a_fails("{}"));
    CHECK(a_fails("not json"));
    CHECK(a_fails(trade.substr(0, trade.size() - 1)));                              // truncated
    CHECK(a_fails("{\"r\":5,\"e\":\"trade\",\"E\":6,\"tid\":1,\"px\":\"100.01\",\"qty\":\"0.500\"}"));  // missing side
    CHECK(a_fails("{\"r\":5,\"e\":\"trade\",\"E\":6,\"tid\":1,\"px\":\"100.01\",\"qty\":\"0.500\",\"side\":\"buy\",\"x\":1}"));  // unknown key
    CHECK(a_fails("{\"r\":5,\"r\":6,\"e\":\"trade\",\"E\":6,\"tid\":1,\"px\":\"100.01\",\"qty\":\"0.500\",\"side\":\"buy\"}"));   // duplicate
    CHECK(a_fails("{\"r\":5, \"e\":\"trade\",\"E\":6,\"tid\":1,\"px\":\"100.01\",\"qty\":\"0.500\",\"side\":\"buy\"}"));         // whitespace
    CHECK(a_fails(trade + " "));
    CHECK(a_fails("{\"r\":5,\"e\":\"trade\",\"E\":6,\"tid\":1,\"px\":100.01,\"qty\":\"0.500\",\"side\":\"buy\"}"));   // number for string
    CHECK(a_fails("{\"r\":\"5\",\"e\":\"trade\",\"E\":6,\"tid\":1,\"px\":\"100.01\",\"qty\":\"0.500\",\"side\":\"buy\"}")); // string for int
    CHECK(a_fails("{\"r\":-5,\"e\":\"trade\",\"E\":6,\"tid\":1,\"px\":\"100.01\",\"qty\":\"0.500\",\"side\":\"buy\"}"));  // sign
    CHECK(a_fails("{\"r\":1e3,\"e\":\"trade\",\"E\":6,\"tid\":1,\"px\":\"100.01\",\"qty\":\"0.500\",\"side\":\"buy\"}"));  // exponent
    CHECK(a_fails("{\"r\":5,\"e\":\"trade\",\"E\":6,\"tid\":1,\"px\":\"100.1\",\"qty\":\"0.500\",\"side\":\"buy\"}"));  // 1 frac digit
    CHECK(a_fails("{\"r\":5,\"e\":\"trade\",\"E\":6,\"tid\":1,\"px\":\"100.01\",\"qty\":\"0.50\",\"side\":\"buy\"}"));  // 2 frac digits
    CHECK(a_fails("{\"r\":5,\"e\":\"trade\",\"E\":6,\"tid\":1,\"px\":\"100.01\",\"qty\":\"0.500\",\"side\":\"hold\"}"));
    CHECK(a_fails("{\"r\":5,\"e\":\"trade\",\"E\":6,\"tid\":1,\"px\":\"1\\u0030\",\"qty\":\"0.500\",\"side\":\"buy\"}"));  // escape
    CHECK(a_fails("{\"r\":5,\"e\":\"quote\",\"E\":6}"));                                                          // unknown type
    CHECK(a_fails("{\"r\":5,\"e\":\"trade\",\"E\":6,\"tid\":1,\"px\":\"100.01\",\"qty\":\"0.500\",\"side\":\"buy\",}"));  // trailing comma
    CHECK(a_fails("{\"r\":99999999999999999999,\"e\":\"trade\",\"E\":6,\"tid\":1,\"px\":\"100.01\",\"qty\":\"0.500\",\"side\":\"buy\"}"));  // overflow
    CHECK(a_fails("{\"r\":5,\"e\":\"trade\",\"E\":9223372036854775,\"tid\":1,\"px\":\"100.01\",\"qty\":\"0.500\",\"side\":\"buy\"}") == false);  // E*1000 just fits
    CHECK(a_fails("{\"r\":5,\"e\":\"trade\",\"E\":9223372036854776,\"tid\":1,\"px\":\"100.01\",\"qty\":\"0.500\",\"side\":\"buy\"}"));  // E*1000 overflows
    // nested object / key not valid for the type
    CHECK(a_fails("{\"r\":5,\"e\":\"trade\",\"E\":{\"x\":1},\"tid\":1,\"px\":\"100.01\",\"qty\":\"0.500\",\"side\":\"buy\"}"));
    CHECK(a_fails("{\"r\":5,\"e\":\"trade\",\"E\":6,\"tid\":1,\"px\":\"100.01\",\"qty\":\"0.500\",\"side\":\"buy\",\"seq\":3}"));
    // depth rules
    CHECK(a_fails("{\"r\":5,\"e\":\"depth\",\"E\":6,\"first_seq\":3,\"last_seq\":2,\"b\":[[\"99.99\",\"1.000\"]],\"a\":[]}"));  // first > last
    CHECK(a_fails("{\"r\":5,\"e\":\"depth\",\"E\":6,\"first_seq\":3,\"last_seq\":3,\"b\":[],\"a\":[]}"));                       // empty diff
    CHECK(a_fails("{\"r\":5,\"e\":\"depth\",\"E\":6,\"first_seq\":3,\"last_seq\":3,\"b\":[[\"99.99\"]],\"a\":[]}"));             // 1-element level
    CHECK(a_fails("{\"r\":5,\"e\":\"depth\",\"E\":6,\"first_seq\":3,\"last_seq\":3,\"b\":[[\"99.99\",\"1.000\",\"x\"]],\"a\":[]}"));
    CHECK(a_fails("{\"r\":5,\"e\":\"depth\",\"E\":6,\"first_seq\":3,\"last_seq\":3,\"b\":[[\"99.99\",\"1.000\"]],\"a\":[]"));    // no closing brace
    CHECK(a_fails("{\"r\":5,\"e\":\"depth\",\"E\":6,\"first_seq\":3,\"last_seq\":3,\"b\":[[\"99.99\",\"1.000\"],],\"a\":[]}"));
    CHECK(a_fails("{\"r\":5,\"e\":\"depth\",\"E\":6,\"first_seq\":3,\"last_seq\":3,\"b\":[[\"99.99\",1]],\"a\":[]}"));
    // snapshot rule: level quantity must be positive
    CHECK(a_fails("{\"r\":5,\"e\":\"snapshot\",\"E\":6,\"seq\":3,\"b\":[[\"99.99\",\"0.000\"]],\"a\":[]}"));
}

TEST(parser_a_error_message_is_reusable) {
    ParserA p;
    std::vector<Event> out;
    CHECK(p.parse("garbage", 0, out) == ParseStatus::Error);
    CHECK(out.empty());
    CHECK(p.parse("{\"r\":5,\"e\":\"trade\",\"E\":6,\"tid\":1,\"px\":\"100.01\",\"qty\":\"0.500\",\"side\":\"buy\"}", 1, out) == ParseStatus::Ok);
    CHECK_EQ(out.size(), static_cast<std::size_t>(1));
    CHECK(p.last_error().empty());
}

TEST(parser_b_rows) {
    const auto sb = b_ok("1700000000001517697,SB,1700000000000300000,5000000,,,,,3", 0);
    CHECK_EQ(csv_of(sb[0]), std::string("B,SNAPSHOT_BEGIN,NONE,0,3,1700000000000300000,1700000000001517697,5000000,5000000,-1,0,1"));
    const auto sl = b_ok("1700000000001517697,SL,1700000000000300000,5000000,,,2,1000100,4300", 2);
    CHECK_EQ(csv_of(sl[0]), std::string("B,SNAPSHOT_LEVEL,ASK,10001,4300,1700000000000300000,1700000000001517697,5000000,5000000,-1,2,1"));
    const auto se = b_ok("1700000000001517697,SE,1700000000000300000,5000000,,,,,", 4);
    CHECK_EQ(csv_of(se[0]), std::string("B,SNAPSHOT_END,NONE,0,0,1700000000000300000,1700000000001517697,5000000,5000000,-1,4,1"));
    const auto t = b_ok("1700000000066722244,T,1700000000065670853,900000,,,1,1000100,200", 5);
    CHECK_EQ(csv_of(t[0]), std::string("B,TRADE,BID,10001,200,1700000000065670853,1700000000066722244,900000,900000,-1,5,1"));
    const auto c = b_ok("1700000000067557785,U,1700000000065670853,5000002,5000000,C,2,1000100,4100", 6);
    CHECK_EQ(csv_of(c[0]), std::string("B,MODIFY,ASK,10001,4100,1700000000065670853,1700000000067557785,5000002,5000002,5000000,6,1"));
    const auto d = b_ok("1700000000134158361,U,1700000000133157628,5000004,5000002,D,1,1000000,0", 7);
    CHECK_EQ(csv_of(d[0]), std::string("B,DELETE,BID,10000,0,1700000000133157628,1700000000134158361,5000004,5000004,5000002,7,1"));
    const auto n = b_ok("1700000000134316532,U,1700000000133157628,5000006,5000004,N,1,999800,4000", 8);
    CHECK_EQ(csv_of(n[0]), std::string("B,ADD,BID,9998,4000,1700000000133157628,1700000000134316532,5000006,5000006,5000004,8,1"));
}

TEST(parser_b_px_e4_scaling) {
    const auto e1 = b_ok("1,T,2,3,,,1,1000100,1");
    const auto e2 = b_ok("1,T,2,3,,,1,10000,1");
    const auto e3 = b_ok("1,T,2,3,,,2,100,7");
    CHECK_EQ(e1[0].price_ticks, 10001);
    CHECK_EQ(e2[0].price_ticks, 100);
    CHECK_EQ(e3[0].price_ticks, 1);
    CHECK(b_fails("1,T,2,3,,,1,1000150,1"));  // not divisible by 100
    CHECK(b_fails("1,T,2,3,,,1,1000101,1"));
}

TEST(parser_b_errors) {
    CHECK(!b_fails("1,T,2,3,,,1,1000100,1"));
    CHECK(b_fails(""));
    CHECK(b_fails("1,T,2,3,,,1,1000100"));            // 8 fields
    CHECK(b_fails("1,T,2,3,,,1,1000100,1,"));         // 10 fields
    CHECK(b_fails("1,X,2,3,,,1,1000100,1"));          // unknown kind
    CHECK(b_fails("1,U,2,3,2,Z,1,1000100,1"));        // unknown action
    CHECK(b_fails("1,U,2,3,2,N,3,1000100,1"));        // unknown side
    CHECK(b_fails("1,U,2,3,2,N,0,1000100,1"));
    CHECK(b_fails("x,T,2,3,,,1,1000100,1"));          // non-integer recv
    CHECK(b_fails("1,T,2x,3,,,1,1000100,1"));
    CHECK(b_fails("1,T,2,-3,,,1,1000100,1"));
    CHECK(b_fails("1,T,2,3,,,1,10.5,1"));
    CHECK(b_fails("1,T,2,3,,,1,1000100,1.5"));
    CHECK(b_fails("1,T,2,3,,,1,1000100, 1"));         // space
    CHECK(b_fails("1,T,2,3,9,,1,1000100,1"));         // T with prev_id
    CHECK(b_fails("1,T,2,3,,N,1,1000100,1"));         // T with action
    CHECK(b_fails("1,SB,2,3,,,1,,3"));                // SB with side
    CHECK(b_fails("1,SB,2,3,,,,100,3"));              // SB with px
    CHECK(b_fails("1,SB,2,3,,,,,"));                  // SB missing count
    CHECK(b_fails("1,SE,2,3,,,,,5"));                 // SE with size
    CHECK(b_fails("1,SL,2,3,,,,1000100,5"));          // SL missing side
    CHECK(b_fails("1,SL,2,3,,,1,,5"));                // SL missing px
    CHECK(b_fails("1,SL,2,3,,,1,1000100,"));          // SL missing size
    CHECK(b_fails("1,SL,2,3,,,1,1000100,0"));         // zero-size snapshot level
    CHECK(b_fails("1,U,2,3,,N,1,1000100,5"));         // U missing prev_id
    CHECK(b_fails("1,U,2,3,2,,1,1000100,5"));         // U missing action
    CHECK(b_fails("1,U,2,3,2,N,1,1000100,0"));        // N with zero size
    CHECK(b_fails("1,U,2,3,2,D,1,1000100,5"));        // D with nonzero size
    CHECK(b_fails(",T,2,3,,,1,1000100,1"));           // missing recv
    CHECK(b_fails("99999999999999999999,T,2,3,,,1,1000100,1"));
}

TEST(split_csv_basic) {
    std::string_view f[4];
    CHECK_EQ(split_csv("a,b,,d", f, 4), static_cast<std::size_t>(4));
    CHECK(f[0] == "a");
    CHECK(f[2].empty());
    CHECK(f[3] == "d");
    CHECK_EQ(split_csv("", f, 4), static_cast<std::size_t>(1));
    CHECK_EQ(split_csv("a,b,c,d,e,f", f, 4), static_cast<std::size_t>(6));  // total count, 4 stored
    CHECK_EQ(split_csv(",", f, 4), static_cast<std::size_t>(2));
}
