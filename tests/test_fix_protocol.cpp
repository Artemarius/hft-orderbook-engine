/// @file test_fix_protocol.cpp
/// @brief Comprehensive tests for FIX 4.2 parser and serializer.

#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include "core/types.h"
#include "feed/fix_message.h"
#include "feed/fix_parser.h"
#include "feed/fix_serializer.h"
#include "transport/message.h"

using namespace hft;
using namespace hft::fix;

// ===========================================================================
// Helpers
// ===========================================================================

/// Build a minimal valid NewOrderSingle (pipe-delimited).
static std::string make_new_order(
    const std::string& cl_ord_id = "ORD001",
    char side = '1',
    const std::string& price = "42000.50",
    const std::string& qty = "10",
    char ord_type = '2',
    char tif = '1',
    const std::string& symbol = "BTCUSDT") {

    // Build body (everything between tag 9 and tag 10)
    std::string body;
    body += "35=D|";
    body += "49=TRADER1|";
    body += "56=HFT-ENGINE|";
    body += "11=" + cl_ord_id + "|";
    body += "55=" + symbol + "|";
    body += std::string("54=") + side + "|";
    body += "60=20240101-00:00:01.000|";
    body += std::string("40=") + ord_type + "|";
    body += "38=" + qty + "|";
    if (ord_type == '2') {
        body += "44=" + price + "|";
    }
    body += std::string("59=") + tif + "|";

    // Wrap with tag 8, 9, 10
    std::string prefix = "8=FIX.4.2|9=" + std::to_string(body.size()) + "|";
    std::string pre_cs = prefix + body;
    uint8_t cs = FixParser::compute_checksum(pre_cs);
    char cs_buf[4];
    std::snprintf(cs_buf, sizeof(cs_buf), "%03u", static_cast<unsigned>(cs));
    return pre_cs + "10=" + cs_buf + "|";
}

/// Build a minimal valid OrderCancelRequest (pipe-delimited).
static std::string make_cancel(
    const std::string& cl_ord_id = "ORD016",
    const std::string& orig_cl_ord_id = "ORD001",
    const std::string& symbol = "BTCUSDT") {

    std::string body;
    body += "35=F|";
    body += "49=TRADER1|";
    body += "56=HFT-ENGINE|";
    body += "11=" + cl_ord_id + "|";
    body += "41=" + orig_cl_ord_id + "|";
    body += "55=" + symbol + "|";
    body += "54=1|";
    body += "60=20240101-00:00:16.000|";
    body += "38=10|";

    std::string prefix = "8=FIX.4.2|9=" + std::to_string(body.size()) + "|";
    std::string pre_cs = prefix + body;
    uint8_t cs = FixParser::compute_checksum(pre_cs);
    char cs_buf[4];
    std::snprintf(cs_buf, sizeof(cs_buf), "%03u", static_cast<unsigned>(cs));
    return pre_cs + "10=" + cs_buf + "|";
}

/// Build a minimal valid OrderCancelReplace (pipe-delimited).
static std::string make_cancel_replace(
    const std::string& cl_ord_id = "ORD021",
    const std::string& orig_cl_ord_id = "ORD003",
    const std::string& price = "42003.50",
    const std::string& qty = "10",
    const std::string& symbol = "BTCUSDT") {

    std::string body;
    body += "35=G|";
    body += "49=TRADER1|";
    body += "56=HFT-ENGINE|";
    body += "11=" + cl_ord_id + "|";
    body += "41=" + orig_cl_ord_id + "|";
    body += "55=" + symbol + "|";
    body += "54=1|";
    body += "60=20240101-00:00:21.000|";
    body += "40=2|";
    body += "38=" + qty + "|";
    body += "44=" + price + "|";
    body += "59=1|";

    std::string prefix = "8=FIX.4.2|9=" + std::to_string(body.size()) + "|";
    std::string pre_cs = prefix + body;
    uint8_t cs = FixParser::compute_checksum(pre_cs);
    char cs_buf[4];
    std::snprintf(cs_buf, sizeof(cs_buf), "%03u", static_cast<unsigned>(cs));
    return pre_cs + "10=" + cs_buf + "|";
}

/// Build an EventMessage of a given type with order event data.
static EventMessage make_order_event(
    EventType type,
    OrderId order_id = 100,
    Price price = 4200050000000LL,
    Quantity filled = 5,
    Quantity remaining = 5,
    uint64_t seq = 1,
    Timestamp ts = 1000000) {

    EventMessage ev{};
    ev.type = type;
    ev.instrument_id = DEFAULT_INSTRUMENT_ID;
    ev.sequence_num = seq;
    ev.data.order_event.order_id = order_id;
    ev.data.order_event.status = OrderStatus::Accepted;
    ev.data.order_event.filled_quantity = filled;
    ev.data.order_event.remaining_quantity = remaining;
    ev.data.order_event.price = price;
    ev.data.order_event.timestamp = ts;
    return ev;
}

/// Build an EventMessage for a Trade.
static EventMessage make_trade_event(
    uint64_t trade_id = 1,
    OrderId buy_id = 100,
    OrderId sell_id = 200,
    Price price = 4200050000000LL,
    Quantity quantity = 5,
    uint64_t seq = 1,
    Timestamp ts = 1000000) {

    EventMessage ev{};
    ev.type = EventType::Trade;
    ev.instrument_id = DEFAULT_INSTRUMENT_ID;
    ev.sequence_num = seq;
    ev.data.trade.trade_id = trade_id;
    ev.data.trade.buy_order_id = buy_id;
    ev.data.trade.sell_order_id = sell_id;
    ev.data.trade.price = price;
    ev.data.trade.quantity = quantity;
    ev.data.trade.timestamp = ts;
    return ev;
}

// ===========================================================================
// Parser — valid messages
// ===========================================================================

TEST(FixParser, ParseNewOrderSingle) {
    auto raw = make_new_order("ORD001", '1', "42000.50", "10");
    auto msg = FixParser::parse(raw);

    EXPECT_TRUE(msg.valid) << msg.error;
    EXPECT_EQ(msg.msg_type, MsgType::NewOrderSingle);
    EXPECT_EQ(msg.begin_string, "FIX.4.2");
    EXPECT_EQ(msg.sender_comp_id, "TRADER1");
    EXPECT_EQ(msg.target_comp_id, "HFT-ENGINE");
    EXPECT_EQ(msg.cl_ord_id, "ORD001");
    EXPECT_EQ(msg.symbol, "BTCUSDT");
    EXPECT_EQ(msg.fix_side, '1');
    EXPECT_EQ(msg.fix_ord_type, '2');
    EXPECT_EQ(msg.fix_tif, '1');
    EXPECT_EQ(msg.price, 42000LL * PRICE_SCALE + 50000000LL);
    EXPECT_EQ(msg.quantity, 10u);
}

TEST(FixParser, ParseSellOrder) {
    auto raw = make_new_order("ORD006", '2', "42005.00", "10");
    auto msg = FixParser::parse(raw);

    EXPECT_TRUE(msg.valid) << msg.error;
    EXPECT_EQ(msg.fix_side, '2');
    EXPECT_EQ(msg.price, 42005LL * PRICE_SCALE);
}

TEST(FixParser, ParseCancelRequest) {
    auto raw = make_cancel("ORD016", "ORD001");
    auto msg = FixParser::parse(raw);

    EXPECT_TRUE(msg.valid) << msg.error;
    EXPECT_EQ(msg.msg_type, MsgType::OrderCancelRequest);
    EXPECT_EQ(msg.cl_ord_id, "ORD016");
    EXPECT_EQ(msg.orig_cl_ord_id, "ORD001");
    EXPECT_EQ(msg.symbol, "BTCUSDT");
}

TEST(FixParser, ParseCancelReplace) {
    auto raw = make_cancel_replace("ORD021", "ORD003", "42003.50", "10");
    auto msg = FixParser::parse(raw);

    EXPECT_TRUE(msg.valid) << msg.error;
    EXPECT_EQ(msg.msg_type, MsgType::OrderCancelReplace);
    EXPECT_EQ(msg.cl_ord_id, "ORD021");
    EXPECT_EQ(msg.orig_cl_ord_id, "ORD003");
    EXPECT_EQ(msg.price, 42003LL * PRICE_SCALE + 50000000LL);
    EXPECT_EQ(msg.quantity, 10u);
}

TEST(FixParser, ParseMarketOrder) {
    auto raw = make_new_order("ORD026", '1', "", "3", '1', '1');
    auto msg = FixParser::parse(raw);

    EXPECT_TRUE(msg.valid) << msg.error;
    EXPECT_EQ(msg.fix_ord_type, '1');  // Market
    EXPECT_EQ(msg.price, 0);          // No price for market orders
}

TEST(FixParser, ParseIOCOrder) {
    auto raw = make_new_order("ORD013", '1', "42007.00", "20", '2', '3');
    auto msg = FixParser::parse(raw);

    EXPECT_TRUE(msg.valid) << msg.error;
    EXPECT_EQ(msg.fix_tif, '3');  // IOC
}

TEST(FixParser, ParseFOKOrder) {
    auto raw = make_new_order("ORD027", '1', "42004.50", "50", '2', '4');
    auto msg = FixParser::parse(raw);

    EXPECT_TRUE(msg.valid) << msg.error;
    EXPECT_EQ(msg.fix_tif, '4');  // FOK
}

TEST(FixParser, ParseDayOrder) {
    auto raw = make_new_order("ORD012", '1', "42005.50", "5", '2', '0');
    auto msg = FixParser::parse(raw);

    EXPECT_TRUE(msg.valid) << msg.error;
    EXPECT_EQ(msg.fix_tif, '0');  // DAY
}

// ===========================================================================
// Parser — checksum
// ===========================================================================

TEST(FixParser, ValidateChecksumCorrect) {
    auto raw = make_new_order();
    EXPECT_TRUE(FixParser::validate_checksum(raw));
}

TEST(FixParser, ValidateChecksumIncorrect) {
    auto raw = make_new_order();
    // Corrupt the checksum
    size_t cs_pos = raw.rfind("10=");
    ASSERT_NE(cs_pos, std::string::npos);
    raw[cs_pos + 3] = '0';
    raw[cs_pos + 4] = '0';
    raw[cs_pos + 5] = '0';

    EXPECT_FALSE(FixParser::validate_checksum(raw));
}

TEST(FixParser, ChecksumMismatchRejectsMessage) {
    auto raw = make_new_order();
    // Corrupt the checksum
    size_t cs_pos = raw.rfind("10=");
    ASSERT_NE(cs_pos, std::string::npos);
    raw[cs_pos + 3] = '0';
    raw[cs_pos + 4] = '0';
    raw[cs_pos + 5] = '0';

    auto msg = FixParser::parse(raw);
    EXPECT_FALSE(msg.valid);
    EXPECT_NE(msg.error.find("checksum"), std::string::npos);
}

TEST(FixParser, ComputeChecksum) {
    // Simple known-value test
    std::string data = "8=FIX.4.2|9=5|35=D|";
    uint8_t cs = FixParser::compute_checksum(data);
    // Verify by computing manually
    uint32_t sum = 0;
    for (char c : data) sum += static_cast<uint8_t>(c);
    EXPECT_EQ(cs, static_cast<uint8_t>(sum % 256));
}

// ===========================================================================
// Parser — BodyLength validation
// ===========================================================================

TEST(FixParser, BodyLengthCorrect) {
    auto raw = make_new_order();
    auto msg = FixParser::parse(raw);
    EXPECT_TRUE(msg.valid) << msg.error;
}

TEST(FixParser, BodyLengthIncorrect) {
    // Build a message with wrong body length
    std::string body = "35=D|49=TRADER1|56=HFT-ENGINE|11=ORD001|55=BTCUSDT|54=1|60=20240101-00:00:01.000|40=2|38=10|44=42000.50|59=1|";
    std::string prefix = "8=FIX.4.2|9=999|";  // Wrong length
    std::string pre_cs = prefix + body;
    uint8_t cs = FixParser::compute_checksum(pre_cs);
    char cs_buf[4];
    std::snprintf(cs_buf, sizeof(cs_buf), "%03u", static_cast<unsigned>(cs));
    std::string raw = pre_cs + "10=" + cs_buf + "|";

    auto msg = FixParser::parse(raw);
    EXPECT_FALSE(msg.valid);
    EXPECT_NE(msg.error.find("BodyLength"), std::string::npos);
}

// ===========================================================================
// Parser — missing required tags
// ===========================================================================

TEST(FixParser, MissingMsgType) {
    std::string raw = "8=FIX.4.2|9=5|49=X|10=000|";
    auto msg = FixParser::parse(raw);
    EXPECT_FALSE(msg.valid);
    EXPECT_NE(msg.error.find("MsgType"), std::string::npos);
}

TEST(FixParser, NewOrderMissingClOrdID) {
    std::string body = "35=D|49=TRADER1|56=HFT-ENGINE|55=BTCUSDT|54=1|40=2|38=10|44=42000.00|59=1|";
    std::string raw = "8=FIX.4.2|9=" + std::to_string(body.size()) + "|" + body + "10=000|";
    auto msg = FixParser::parse(raw);
    EXPECT_FALSE(msg.valid);
    EXPECT_NE(msg.error.find("ClOrdID"), std::string::npos);
}

TEST(FixParser, NewOrderMissingSide) {
    std::string body = "35=D|49=TRADER1|56=HFT-ENGINE|11=ORD001|55=BTCUSDT|40=2|38=10|44=42000.00|59=1|";
    std::string raw = "8=FIX.4.2|9=" + std::to_string(body.size()) + "|" + body + "10=000|";
    auto msg = FixParser::parse(raw);
    EXPECT_FALSE(msg.valid);
    EXPECT_NE(msg.error.find("Side"), std::string::npos);
}

TEST(FixParser, NewOrderMissingOrdType) {
    std::string body = "35=D|49=TRADER1|56=HFT-ENGINE|11=ORD001|55=BTCUSDT|54=1|38=10|44=42000.00|59=1|";
    std::string raw = "8=FIX.4.2|9=" + std::to_string(body.size()) + "|" + body + "10=000|";
    auto msg = FixParser::parse(raw);
    EXPECT_FALSE(msg.valid);
    EXPECT_NE(msg.error.find("OrdType"), std::string::npos);
}

TEST(FixParser, NewOrderMissingQuantity) {
    std::string body = "35=D|49=TRADER1|56=HFT-ENGINE|11=ORD001|55=BTCUSDT|54=1|40=2|44=42000.00|59=1|";
    std::string raw = "8=FIX.4.2|9=" + std::to_string(body.size()) + "|" + body + "10=000|";
    auto msg = FixParser::parse(raw);
    EXPECT_FALSE(msg.valid);
    EXPECT_NE(msg.error.find("OrderQty"), std::string::npos);
}

TEST(FixParser, NewOrderMissingSymbol) {
    std::string body = "35=D|49=TRADER1|56=HFT-ENGINE|11=ORD001|54=1|40=2|38=10|44=42000.00|59=1|";
    std::string raw = "8=FIX.4.2|9=" + std::to_string(body.size()) + "|" + body + "10=000|";
    auto msg = FixParser::parse(raw);
    EXPECT_FALSE(msg.valid);
    EXPECT_NE(msg.error.find("Symbol"), std::string::npos);
}

TEST(FixParser, LimitOrderMissingPrice) {
    std::string body = "35=D|49=TRADER1|56=HFT-ENGINE|11=ORD001|55=BTCUSDT|54=1|40=2|38=10|59=1|";
    std::string raw = "8=FIX.4.2|9=" + std::to_string(body.size()) + "|" + body + "10=000|";
    auto msg = FixParser::parse(raw);
    EXPECT_FALSE(msg.valid);
    EXPECT_NE(msg.error.find("Price"), std::string::npos);
}

TEST(FixParser, CancelMissingOrigClOrdID) {
    std::string body = "35=F|49=TRADER1|56=HFT-ENGINE|11=ORD016|55=BTCUSDT|54=1|60=20240101-00:00:16.000|";
    std::string raw = "8=FIX.4.2|9=" + std::to_string(body.size()) + "|" + body + "10=000|";
    auto msg = FixParser::parse(raw);
    EXPECT_FALSE(msg.valid);
    EXPECT_NE(msg.error.find("OrigClOrdID"), std::string::npos);
}

TEST(FixParser, CancelReplaceMissingOrigClOrdID) {
    std::string body = "35=G|49=TRADER1|56=HFT-ENGINE|11=ORD021|55=BTCUSDT|54=1|40=2|38=10|44=42003.50|59=1|";
    std::string raw = "8=FIX.4.2|9=" + std::to_string(body.size()) + "|" + body + "10=000|";
    auto msg = FixParser::parse(raw);
    EXPECT_FALSE(msg.valid);
    EXPECT_NE(msg.error.find("OrigClOrdID"), std::string::npos);
}

// ===========================================================================
// Parser — enum mapping
// ===========================================================================

TEST(FixParser, SideMapping) {
    EXPECT_EQ(FixParser::parse_side('1'), Side::Buy);
    EXPECT_EQ(FixParser::parse_side('2'), Side::Sell);
    EXPECT_EQ(FixParser::parse_side('9'), Side::Buy);  // fallback
}

TEST(FixParser, OrdTypeMapping) {
    EXPECT_EQ(FixParser::parse_ord_type('1'), OrderType::Market);
    EXPECT_EQ(FixParser::parse_ord_type('2'), OrderType::Limit);
    EXPECT_EQ(FixParser::parse_ord_type('9'), OrderType::Limit);  // fallback
}

TEST(FixParser, TifMapping) {
    EXPECT_EQ(FixParser::parse_tif('0'), TimeInForce::DAY);
    EXPECT_EQ(FixParser::parse_tif('1'), TimeInForce::GTC);
    EXPECT_EQ(FixParser::parse_tif('3'), TimeInForce::IOC);
    EXPECT_EQ(FixParser::parse_tif('4'), TimeInForce::FOK);
    EXPECT_EQ(FixParser::parse_tif('9'), TimeInForce::GTC);  // fallback
}

// ===========================================================================
// Parser — price conversion
// ===========================================================================

TEST(FixParser, PriceConversion_42000_50) {
    auto raw = make_new_order("ORD001", '1', "42000.50", "10");
    auto msg = FixParser::parse(raw);
    EXPECT_TRUE(msg.valid) << msg.error;
    EXPECT_EQ(msg.price, 42000LL * PRICE_SCALE + 50000000LL);
}

TEST(FixParser, PriceConversion_SmallDecimal) {
    auto raw = make_new_order("ORD001", '1', "0.00015000", "10");
    auto msg = FixParser::parse(raw);
    EXPECT_TRUE(msg.valid) << msg.error;
    EXPECT_EQ(msg.price, 15000LL);
}

TEST(FixParser, PriceConversion_IntegerPrice) {
    auto raw = make_new_order("ORD001", '1', "42000", "10");
    auto msg = FixParser::parse(raw);
    EXPECT_TRUE(msg.valid) << msg.error;
    EXPECT_EQ(msg.price, 42000LL * PRICE_SCALE);
}

// ===========================================================================
// Parser — ClOrdID hashing
// ===========================================================================

TEST(FixParser, ClOrdIdHashDeterministic) {
    OrderId h1 = FixParser::cl_ord_id_to_order_id("ORD001");
    OrderId h2 = FixParser::cl_ord_id_to_order_id("ORD001");
    EXPECT_EQ(h1, h2);
}

TEST(FixParser, ClOrdIdHashDifferentStrings) {
    OrderId h1 = FixParser::cl_ord_id_to_order_id("ORD001");
    OrderId h2 = FixParser::cl_ord_id_to_order_id("ORD002");
    EXPECT_NE(h1, h2);
}

TEST(FixParser, ClOrdIdHashNonZero) {
    OrderId h = FixParser::cl_ord_id_to_order_id("ORD001");
    EXPECT_NE(h, 0u);
}

// ===========================================================================
// Parser — pipe to SOH conversion
// ===========================================================================

TEST(FixParser, PipeToSoh) {
    std::string pipe = "8=FIX.4.2|35=D|";
    std::string soh = FixParser::pipe_to_soh(pipe);
    // "8=FIX.4.2" is 9 chars (0-8), pipe at index 9
    // "35=D" is 4 chars (10-13), pipe at index 14
    EXPECT_EQ(soh[9], '\x01');
    EXPECT_EQ(soh[14], '\x01');
    EXPECT_EQ(soh.size(), pipe.size());
    // No pipe chars remain
    EXPECT_EQ(soh.find('|'), std::string::npos);
}

// ===========================================================================
// Parser — malformed messages
// ===========================================================================

TEST(FixParser, EmptyMessage) {
    auto msg = FixParser::parse("");
    EXPECT_FALSE(msg.valid);
}

TEST(FixParser, NoDelimiters) {
    auto msg = FixParser::parse("8=FIX.4.235=D");
    EXPECT_FALSE(msg.valid);  // No MsgType found without delimiters
}

TEST(FixParser, UnsupportedMsgType) {
    std::string body = "35=A|";
    std::string raw = "8=FIX.4.2|9=" + std::to_string(body.size()) + "|" + body + "10=000|";
    auto msg = FixParser::parse(raw);
    EXPECT_FALSE(msg.valid);
    EXPECT_NE(msg.error.find("unsupported"), std::string::npos);
}

TEST(FixParser, TruncatedMessage) {
    auto msg = FixParser::parse("8=FIX.4.2|35=");
    EXPECT_FALSE(msg.valid);
}

// ===========================================================================
// Parser — SOH-delimited messages
// ===========================================================================

TEST(FixParser, ParseSohDelimited) {
    // Build a SOH-delimited message with correct checksum.
    // We can't just pipe_to_soh a pipe-delimited message because the checksum
    // changes when the delimiter bytes change (pipe=0x7C vs SOH=0x01).
    constexpr char S = '\x01';
    std::string body;
    body += "35=D"; body += S;
    body += "49=TRADER1"; body += S;
    body += "56=HFT-ENGINE"; body += S;
    body += "11=ORD001"; body += S;
    body += "55=BTCUSDT"; body += S;
    body += "54=1"; body += S;
    body += "60=20240101-00:00:01.000"; body += S;
    body += "40=2"; body += S;
    body += "38=10"; body += S;
    body += "44=42000.50"; body += S;
    body += "59=1"; body += S;

    std::string prefix;
    prefix += "8=FIX.4.2"; prefix += S;
    prefix += "9=" + std::to_string(body.size()); prefix += S;

    std::string pre_cs = prefix + body;
    uint8_t cs = FixParser::compute_checksum(pre_cs);
    char cs_buf[4];
    std::snprintf(cs_buf, sizeof(cs_buf), "%03u", static_cast<unsigned>(cs));
    std::string raw = pre_cs + "10=" + std::string(cs_buf) + S;

    auto msg = FixParser::parse(raw);
    EXPECT_TRUE(msg.valid) << msg.error;
    EXPECT_EQ(msg.msg_type, MsgType::NewOrderSingle);
    EXPECT_EQ(msg.cl_ord_id, "ORD001");
    EXPECT_EQ(msg.symbol, "BTCUSDT");
}

// ===========================================================================
// Parser — to_order_message
// ===========================================================================

TEST(FixParser, ToOrderMessage_NewOrder) {
    auto raw = make_new_order("ORD001", '1', "42000.50", "10", '2', '1');
    auto msg = FixParser::parse(raw);
    ASSERT_TRUE(msg.valid) << msg.error;

    auto om = FixParser::to_order_message(msg);
    EXPECT_EQ(om.type, MessageType::Add);
    EXPECT_EQ(om.order.side, Side::Buy);
    EXPECT_EQ(om.order.type, OrderType::Limit);
    EXPECT_EQ(om.order.time_in_force, TimeInForce::GTC);
    EXPECT_EQ(om.order.price, 42000LL * PRICE_SCALE + 50000000LL);
    EXPECT_EQ(om.order.quantity, 10u);
    EXPECT_EQ(om.order.order_id, FixParser::cl_ord_id_to_order_id("ORD001"));
}

TEST(FixParser, ToOrderMessage_Cancel) {
    auto raw = make_cancel("ORD016", "ORD001");
    auto msg = FixParser::parse(raw);
    ASSERT_TRUE(msg.valid) << msg.error;

    auto om = FixParser::to_order_message(msg);
    EXPECT_EQ(om.type, MessageType::Cancel);
    // Cancel uses OrigClOrdID for order lookup
    EXPECT_EQ(om.order.order_id, FixParser::cl_ord_id_to_order_id("ORD001"));
}

TEST(FixParser, ToOrderMessage_Modify) {
    auto raw = make_cancel_replace("ORD021", "ORD003", "42003.50", "10");
    auto msg = FixParser::parse(raw);
    ASSERT_TRUE(msg.valid) << msg.error;

    auto om = FixParser::to_order_message(msg);
    EXPECT_EQ(om.type, MessageType::Modify);
    // Modify uses OrigClOrdID for order lookup
    EXPECT_EQ(om.order.order_id, FixParser::cl_ord_id_to_order_id("ORD003"));
    EXPECT_EQ(om.order.price, 42003LL * PRICE_SCALE + 50000000LL);
    EXPECT_EQ(om.order.quantity, 10u);
}

TEST(FixParser, ToOrderMessage_IOC) {
    auto raw = make_new_order("ORD013", '1', "42007.00", "20", '2', '3');
    auto msg = FixParser::parse(raw);
    ASSERT_TRUE(msg.valid) << msg.error;

    auto om = FixParser::to_order_message(msg);
    EXPECT_EQ(om.type, MessageType::Add);
    EXPECT_EQ(om.order.type, OrderType::IOC);
    EXPECT_EQ(om.order.time_in_force, TimeInForce::IOC);
}

TEST(FixParser, ToOrderMessage_FOK) {
    auto raw = make_new_order("ORD027", '1', "42004.50", "50", '2', '4');
    auto msg = FixParser::parse(raw);
    ASSERT_TRUE(msg.valid) << msg.error;

    auto om = FixParser::to_order_message(msg);
    EXPECT_EQ(om.type, MessageType::Add);
    EXPECT_EQ(om.order.type, OrderType::FOK);
    EXPECT_EQ(om.order.time_in_force, TimeInForce::FOK);
}

TEST(FixParser, ToOrderMessage_MarketOrder) {
    auto raw = make_new_order("ORD026", '1', "", "3", '1', '1');
    auto msg = FixParser::parse(raw);
    ASSERT_TRUE(msg.valid) << msg.error;

    auto om = FixParser::to_order_message(msg);
    EXPECT_EQ(om.order.type, OrderType::Market);
}

TEST(FixParser, ToOrderMessage_SellSide) {
    auto raw = make_new_order("ORD006", '2', "42005.00", "10");
    auto msg = FixParser::parse(raw);
    ASSERT_TRUE(msg.valid) << msg.error;

    auto om = FixParser::to_order_message(msg);
    EXPECT_EQ(om.order.side, Side::Sell);
}

TEST(FixParser, ToOrderMessage_InstrumentId) {
    auto raw = make_new_order("ORD001", '1', "42000.50", "10");
    auto msg = FixParser::parse(raw);
    ASSERT_TRUE(msg.valid) << msg.error;

    auto om = FixParser::to_order_message(msg, 42);
    EXPECT_EQ(om.instrument_id, 42u);
    EXPECT_EQ(om.order.instrument_id, 42u);
}

// ===========================================================================
// Serializer — enum mapping
// ===========================================================================

TEST(FixSerializer, SideToFix) {
    EXPECT_EQ(FixSerializer::to_fix_side(Side::Buy), '1');
    EXPECT_EQ(FixSerializer::to_fix_side(Side::Sell), '2');
}

TEST(FixSerializer, ExecTypeMapping) {
    EXPECT_EQ(FixSerializer::to_fix_exec_type(EventType::OrderAccepted), ExecTypeValue::New);
    EXPECT_EQ(FixSerializer::to_fix_exec_type(EventType::OrderPartialFill), ExecTypeValue::PartialFill);
    EXPECT_EQ(FixSerializer::to_fix_exec_type(EventType::OrderFilled), ExecTypeValue::Fill);
    EXPECT_EQ(FixSerializer::to_fix_exec_type(EventType::Trade), ExecTypeValue::Fill);
    EXPECT_EQ(FixSerializer::to_fix_exec_type(EventType::OrderCancelled), ExecTypeValue::Cancelled);
    EXPECT_EQ(FixSerializer::to_fix_exec_type(EventType::OrderRejected), ExecTypeValue::Rejected);
    EXPECT_EQ(FixSerializer::to_fix_exec_type(EventType::OrderModified), ExecTypeValue::Replaced);
}

TEST(FixSerializer, OrdStatusMapping) {
    EXPECT_EQ(FixSerializer::to_fix_ord_status(EventType::OrderAccepted), OrdStatusValue::New);
    EXPECT_EQ(FixSerializer::to_fix_ord_status(EventType::OrderPartialFill), OrdStatusValue::PartialFill);
    EXPECT_EQ(FixSerializer::to_fix_ord_status(EventType::OrderFilled), OrdStatusValue::Filled);
    EXPECT_EQ(FixSerializer::to_fix_ord_status(EventType::OrderCancelled), OrdStatusValue::Cancelled);
    EXPECT_EQ(FixSerializer::to_fix_ord_status(EventType::OrderRejected), OrdStatusValue::Rejected);
    EXPECT_EQ(FixSerializer::to_fix_ord_status(EventType::OrderModified), OrdStatusValue::Replaced);
}

// ===========================================================================
// Serializer — price formatting
// ===========================================================================

TEST(FixSerializer, FormatPrice_Zero) {
    EXPECT_EQ(FixSerializer::format_price(0), "0.00000000");
}

TEST(FixSerializer, FormatPrice_42000_50) {
    Price p = 42000LL * PRICE_SCALE + 50000000LL;
    EXPECT_EQ(FixSerializer::format_price(p), "42000.50000000");
}

TEST(FixSerializer, FormatPrice_Integer) {
    Price p = 42000LL * PRICE_SCALE;
    EXPECT_EQ(FixSerializer::format_price(p), "42000.00000000");
}

TEST(FixSerializer, FormatPrice_SmallDecimal) {
    Price p = 15000LL;  // 0.00015000
    EXPECT_EQ(FixSerializer::format_price(p), "0.00015000");
}

// ===========================================================================
// Serializer — execution reports
// ===========================================================================

TEST(FixSerializer, OrderAcceptedReport) {
    auto ev = make_order_event(EventType::OrderAccepted, 100, 42000LL * PRICE_SCALE, 0, 10);
    auto fix_str = FixSerializer::to_execution_report_pretty(ev, "BTCUSDT");

    EXPECT_NE(fix_str.find("35=8"), std::string::npos);
    EXPECT_NE(fix_str.find("150=0"), std::string::npos);  // ExecType=New
    EXPECT_NE(fix_str.find("39=0"), std::string::npos);   // OrdStatus=New
    EXPECT_NE(fix_str.find("55=BTCUSDT"), std::string::npos);
    EXPECT_NE(fix_str.find("14=0"), std::string::npos);   // CumQty=0
    EXPECT_NE(fix_str.find("151=10"), std::string::npos);  // LeavesQty=10
}

TEST(FixSerializer, OrderCancelledReport) {
    auto ev = make_order_event(EventType::OrderCancelled, 100, 42000LL * PRICE_SCALE, 0, 0);
    auto fix_str = FixSerializer::to_execution_report_pretty(ev, "BTCUSDT");

    EXPECT_NE(fix_str.find("150=4"), std::string::npos);  // ExecType=Cancelled
    EXPECT_NE(fix_str.find("39=4"), std::string::npos);   // OrdStatus=Cancelled
}

TEST(FixSerializer, OrderRejectedReport) {
    auto ev = make_order_event(EventType::OrderRejected, 100, 42000LL * PRICE_SCALE, 0, 0);
    auto fix_str = FixSerializer::to_execution_report_pretty(ev, "BTCUSDT");

    EXPECT_NE(fix_str.find("150=8"), std::string::npos);  // ExecType=Rejected
    EXPECT_NE(fix_str.find("39=8"), std::string::npos);   // OrdStatus=Rejected
}

TEST(FixSerializer, OrderFilledReport) {
    auto ev = make_order_event(EventType::OrderFilled, 100, 42000LL * PRICE_SCALE, 10, 0);
    auto fix_str = FixSerializer::to_execution_report_pretty(ev, "BTCUSDT");

    EXPECT_NE(fix_str.find("150=2"), std::string::npos);  // ExecType=Fill
    EXPECT_NE(fix_str.find("39=2"), std::string::npos);   // OrdStatus=Filled
    EXPECT_NE(fix_str.find("14=10"), std::string::npos);  // CumQty=10
    EXPECT_NE(fix_str.find("151=0"), std::string::npos);  // LeavesQty=0
}

TEST(FixSerializer, OrderPartialFillReport) {
    auto ev = make_order_event(EventType::OrderPartialFill, 100, 42000LL * PRICE_SCALE, 5, 5);
    auto fix_str = FixSerializer::to_execution_report_pretty(ev, "BTCUSDT");

    EXPECT_NE(fix_str.find("150=1"), std::string::npos);  // ExecType=PartialFill
    EXPECT_NE(fix_str.find("39=1"), std::string::npos);   // OrdStatus=PartialFill
    EXPECT_NE(fix_str.find("14=5"), std::string::npos);   // CumQty=5
    EXPECT_NE(fix_str.find("151=5"), std::string::npos);  // LeavesQty=5
}

TEST(FixSerializer, OrderModifiedReport) {
    auto ev = make_order_event(EventType::OrderModified, 100, 42003LL * PRICE_SCALE + 50000000LL, 0, 10);
    auto fix_str = FixSerializer::to_execution_report_pretty(ev, "BTCUSDT");

    EXPECT_NE(fix_str.find("150=5"), std::string::npos);  // ExecType=Replaced
    EXPECT_NE(fix_str.find("39=5"), std::string::npos);   // OrdStatus=Replaced
}

TEST(FixSerializer, TradeReport) {
    auto ev = make_trade_event(1, 100, 200, 42005LL * PRICE_SCALE, 5);
    auto fix_str = FixSerializer::to_execution_report_pretty(ev, "BTCUSDT");

    EXPECT_NE(fix_str.find("35=8"), std::string::npos);
    EXPECT_NE(fix_str.find("150=2"), std::string::npos);  // ExecType=Fill
    EXPECT_NE(fix_str.find("32=5"), std::string::npos);   // LastQty=5
    EXPECT_NE(fix_str.find("31=42005.00000000"), std::string::npos);  // LastPx
}

// ===========================================================================
// Serializer — checksum and body length correctness
// ===========================================================================

TEST(FixSerializer, ChecksumCorrectness) {
    auto ev = make_order_event(EventType::OrderAccepted);
    auto fix_str = FixSerializer::to_execution_report_pretty(ev, "BTCUSDT");

    // Re-validate the serialized message's checksum
    EXPECT_TRUE(FixParser::validate_checksum(fix_str));
}

TEST(FixSerializer, BodyLengthCorrectness) {
    auto ev = make_order_event(EventType::OrderFilled, 100, 42000LL * PRICE_SCALE, 10, 0);
    auto fix_str = FixSerializer::to_execution_report_pretty(ev, "BTCUSDT");

    // Parse and check body length is accepted
    auto msg = FixParser::parse(fix_str);
    // Body length validation happens in parse; if valid, it passed
    EXPECT_TRUE(msg.valid) << msg.error;
}

TEST(FixSerializer, SohOutputChecksumCorrectness) {
    auto ev = make_trade_event();
    auto fix_str = FixSerializer::to_execution_report(ev, "BTCUSDT");

    // Should be SOH-delimited
    EXPECT_NE(fix_str.find('\x01'), std::string::npos);
    EXPECT_TRUE(FixParser::validate_checksum(fix_str));
}

// ===========================================================================
// Serializer — round-trip
// ===========================================================================

TEST(FixRoundTrip, SerializeAndReparse) {
    auto ev = make_order_event(EventType::OrderAccepted, 12345, 42000LL * PRICE_SCALE + 50000000LL, 0, 10, 42, 999);
    auto fix_str = FixSerializer::to_execution_report_pretty(ev, "BTCUSDT", "HFT-ENGINE", "CLIENT");

    auto reparsed = FixParser::parse(fix_str);
    ASSERT_TRUE(reparsed.valid) << reparsed.error;

    EXPECT_EQ(reparsed.msg_type, MsgType::ExecutionReport);
    EXPECT_EQ(reparsed.begin_string, "FIX.4.2");
    EXPECT_EQ(reparsed.sender_comp_id, "HFT-ENGINE");
    EXPECT_EQ(reparsed.target_comp_id, "CLIENT");
    EXPECT_EQ(reparsed.symbol, "BTCUSDT");
}

TEST(FixRoundTrip, TradeSerializeAndReparse) {
    auto ev = make_trade_event(42, 100, 200, 42005LL * PRICE_SCALE, 8, 7, 5000);
    auto fix_str = FixSerializer::to_execution_report_pretty(ev, "ETHUSDT", "ENGINE", "RISK");

    auto reparsed = FixParser::parse(fix_str);
    ASSERT_TRUE(reparsed.valid) << reparsed.error;

    EXPECT_EQ(reparsed.msg_type, MsgType::ExecutionReport);
    EXPECT_EQ(reparsed.sender_comp_id, "ENGINE");
    EXPECT_EQ(reparsed.target_comp_id, "RISK");
    EXPECT_EQ(reparsed.symbol, "ETHUSDT");
}

// ===========================================================================
// Parser — sample data file
// ===========================================================================

TEST(FixParser, ParseSampleDataMessages) {
    // Parse first few messages from the sample file to ensure they are valid
    std::string msg1 = "8=FIX.4.2|9=148|35=D|49=TRADER1|56=HFT-ENGINE|34=1|"
                       "52=20240101-00:00:01.000|11=ORD001|1=ACCT01|55=BTCUSDT|"
                       "54=1|60=20240101-00:00:01.000|40=2|38=10|44=42000.00|59=1|"
                       "10=139|";
    auto parsed = FixParser::parse(msg1);
    EXPECT_TRUE(parsed.valid) << parsed.error;
    EXPECT_EQ(parsed.msg_type, MsgType::NewOrderSingle);
    EXPECT_EQ(parsed.cl_ord_id, "ORD001");
    EXPECT_EQ(parsed.symbol, "BTCUSDT");
    EXPECT_EQ(parsed.fix_side, '1');
    EXPECT_EQ(parsed.price, 42000LL * PRICE_SCALE);
    EXPECT_EQ(parsed.quantity, 10u);
}

TEST(FixParser, ParseSampleCancelMessage) {
    std::string msg16 = "8=FIX.4.2|9=128|35=F|49=TRADER1|56=HFT-ENGINE|34=16|"
                        "52=20240101-00:00:16.000|11=ORD016|41=ORD001|55=BTCUSDT|"
                        "54=1|60=20240101-00:00:16.000|38=10|10=181|";
    auto parsed = FixParser::parse(msg16);
    EXPECT_TRUE(parsed.valid) << parsed.error;
    EXPECT_EQ(parsed.msg_type, MsgType::OrderCancelRequest);
    EXPECT_EQ(parsed.orig_cl_ord_id, "ORD001");
}

TEST(FixParser, ParseSampleCancelReplaceMessage) {
    std::string msg21 = "8=FIX.4.2|9=150|35=G|49=TRADER1|56=HFT-ENGINE|34=21|"
                        "52=20240101-00:00:21.000|11=ORD021|41=ORD003|55=BTCUSDT|"
                        "54=1|60=20240101-00:00:21.000|40=2|38=10|44=42003.50|59=1|"
                        "10=247|";
    auto parsed = FixParser::parse(msg21);
    EXPECT_TRUE(parsed.valid) << parsed.error;
    EXPECT_EQ(parsed.msg_type, MsgType::OrderCancelReplace);
    EXPECT_EQ(parsed.orig_cl_ord_id, "ORD003");
    EXPECT_EQ(parsed.price, 42003LL * PRICE_SCALE + 50000000LL);
}
