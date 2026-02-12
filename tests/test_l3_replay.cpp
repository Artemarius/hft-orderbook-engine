/// @file test_l3_replay.cpp
/// @brief Unit and integration tests for L3FeedParser and ReplayEngine (Phase 6).

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/types.h"
#include "feed/l3_feed_parser.h"
#include "feed/replay_engine.h"
#include "transport/message.h"

using namespace hft;

// ===========================================================================
// Helpers
// ===========================================================================

/// Write a temporary CSV file and return its path.
static std::string write_temp_csv(const std::string& content) {
    // Use a fixed temp path for simplicity (tests run sequentially)
    std::string path = "test_l3_temp.csv";
    std::ofstream out(path);
    out << content;
    out.close();
    return path;
}

/// Clean up the temp CSV file.
static void remove_temp_csv(const std::string& path) {
    std::remove(path.c_str());
}

// ===========================================================================
// Price parsing tests
// ===========================================================================

TEST(L3PriceParsing, IntegerPrice) {
    EXPECT_EQ(L3FeedParser::parse_price("42000"), 42000LL * PRICE_SCALE);
}

TEST(L3PriceParsing, DecimalPrice) {
    EXPECT_EQ(L3FeedParser::parse_price("42000.50"),
              42000LL * PRICE_SCALE + 50000000LL);
}

TEST(L3PriceParsing, TwoDecimals) {
    EXPECT_EQ(L3FeedParser::parse_price("42150.25"),
              42150LL * PRICE_SCALE + 25000000LL);
}

TEST(L3PriceParsing, EightDecimals) {
    EXPECT_EQ(L3FeedParser::parse_price("1.12345678"),
              1LL * PRICE_SCALE + 12345678LL);
}

TEST(L3PriceParsing, OneDecimal) {
    EXPECT_EQ(L3FeedParser::parse_price("100.5"),
              100LL * PRICE_SCALE + 50000000LL);
}

TEST(L3PriceParsing, ZeroPrice) {
    EXPECT_EQ(L3FeedParser::parse_price("0"), 0);
}

TEST(L3PriceParsing, SmallDecimal) {
    EXPECT_EQ(L3FeedParser::parse_price("0.01"), PRICE_SCALE / 100);
}

TEST(L3PriceParsing, EmptyString) {
    EXPECT_EQ(L3FeedParser::parse_price(""), 0);
}

TEST(L3PriceParsing, InvalidChars) {
    EXPECT_EQ(L3FeedParser::parse_price("abc"), 0);
}

TEST(L3PriceParsing, MixedInvalidChars) {
    EXPECT_EQ(L3FeedParser::parse_price("42x00"), 0);
}

TEST(L3PriceParsing, TrailingDot) {
    // "42000." should parse as 42000 with 0 fractional part
    EXPECT_EQ(L3FeedParser::parse_price("42000."), 42000LL * PRICE_SCALE);
}

TEST(L3PriceParsing, NegativePrice) {
    EXPECT_EQ(L3FeedParser::parse_price("-100.50"),
              -(100LL * PRICE_SCALE + 50000000LL));
}

// ===========================================================================
// Quantity parsing tests
// ===========================================================================

TEST(L3QuantityParsing, ValidInteger) {
    EXPECT_EQ(L3FeedParser::parse_quantity("10"), 10u);
}

TEST(L3QuantityParsing, LargeQuantity) {
    EXPECT_EQ(L3FeedParser::parse_quantity("100000"), 100000u);
}

TEST(L3QuantityParsing, One) {
    EXPECT_EQ(L3FeedParser::parse_quantity("1"), 1u);
}

TEST(L3QuantityParsing, EmptyReturnsZero) {
    EXPECT_EQ(L3FeedParser::parse_quantity(""), 0u);
}

TEST(L3QuantityParsing, InvalidChars) {
    EXPECT_EQ(L3FeedParser::parse_quantity("abc"), 0u);
}

TEST(L3QuantityParsing, DecimalRejected) {
    EXPECT_EQ(L3FeedParser::parse_quantity("10.5"), 0u);
}

// ===========================================================================
// Event type parsing tests
// ===========================================================================

TEST(L3EventTypeParsing, Add) {
    EXPECT_EQ(L3FeedParser::parse_event_type("ADD"), L3EventType::Add);
}

TEST(L3EventTypeParsing, Cancel) {
    EXPECT_EQ(L3FeedParser::parse_event_type("CANCEL"), L3EventType::Cancel);
}

TEST(L3EventTypeParsing, Trade) {
    EXPECT_EQ(L3FeedParser::parse_event_type("TRADE"), L3EventType::Trade);
}

TEST(L3EventTypeParsing, CaseInsensitiveAdd) {
    EXPECT_EQ(L3FeedParser::parse_event_type("add"), L3EventType::Add);
    EXPECT_EQ(L3FeedParser::parse_event_type("Add"), L3EventType::Add);
}

TEST(L3EventTypeParsing, CaseInsensitiveCancel) {
    EXPECT_EQ(L3FeedParser::parse_event_type("cancel"), L3EventType::Cancel);
    EXPECT_EQ(L3FeedParser::parse_event_type("Cancel"), L3EventType::Cancel);
}

TEST(L3EventTypeParsing, CaseInsensitiveTrade) {
    EXPECT_EQ(L3FeedParser::parse_event_type("trade"), L3EventType::Trade);
    EXPECT_EQ(L3FeedParser::parse_event_type("Trade"), L3EventType::Trade);
}

TEST(L3EventTypeParsing, Invalid) {
    EXPECT_EQ(L3FeedParser::parse_event_type("MODIFY"), L3EventType::Invalid);
    EXPECT_EQ(L3FeedParser::parse_event_type(""), L3EventType::Invalid);
    EXPECT_EQ(L3FeedParser::parse_event_type("X"), L3EventType::Invalid);
}

// ===========================================================================
// Side parsing tests
// ===========================================================================

TEST(L3SideParsing, Buy) {
    bool ok = false;
    EXPECT_EQ(L3FeedParser::parse_side("BUY", ok), Side::Buy);
    EXPECT_TRUE(ok);
}

TEST(L3SideParsing, Sell) {
    bool ok = false;
    EXPECT_EQ(L3FeedParser::parse_side("SELL", ok), Side::Sell);
    EXPECT_TRUE(ok);
}

TEST(L3SideParsing, CaseInsensitiveBuy) {
    bool ok = false;
    EXPECT_EQ(L3FeedParser::parse_side("buy", ok), Side::Buy);
    EXPECT_TRUE(ok);
    ok = false;
    EXPECT_EQ(L3FeedParser::parse_side("Buy", ok), Side::Buy);
    EXPECT_TRUE(ok);
}

TEST(L3SideParsing, CaseInsensitiveSell) {
    bool ok = false;
    EXPECT_EQ(L3FeedParser::parse_side("sell", ok), Side::Sell);
    EXPECT_TRUE(ok);
}

TEST(L3SideParsing, InvalidSide) {
    bool ok = true;
    L3FeedParser::parse_side("INVALID", ok);
    EXPECT_FALSE(ok);
}

TEST(L3SideParsing, EmptySide) {
    bool ok = true;
    L3FeedParser::parse_side("", ok);
    EXPECT_FALSE(ok);
}

// ===========================================================================
// CSV split tests
// ===========================================================================

TEST(L3CsvSplit, SixFields) {
    auto fields = L3FeedParser::split_csv(
        "1704067200000000000,ADD,1,BUY,42150.50,10");
    ASSERT_EQ(fields.size(), 6u);
    EXPECT_EQ(fields[0], "1704067200000000000");
    EXPECT_EQ(fields[1], "ADD");
    EXPECT_EQ(fields[2], "1");
    EXPECT_EQ(fields[3], "BUY");
    EXPECT_EQ(fields[4], "42150.50");
    EXPECT_EQ(fields[5], "10");
}

TEST(L3CsvSplit, EmptyFields) {
    auto fields = L3FeedParser::split_csv("1704067200000000000,CANCEL,1,,,");
    ASSERT_EQ(fields.size(), 6u);
    EXPECT_EQ(fields[0], "1704067200000000000");
    EXPECT_EQ(fields[1], "CANCEL");
    EXPECT_EQ(fields[2], "1");
    EXPECT_EQ(fields[3], "");
    EXPECT_EQ(fields[4], "");
    EXPECT_EQ(fields[5], "");
}

TEST(L3CsvSplit, SingleField) {
    auto fields = L3FeedParser::split_csv("hello");
    ASSERT_EQ(fields.size(), 1u);
    EXPECT_EQ(fields[0], "hello");
}

// ===========================================================================
// Full line parsing tests
// ===========================================================================

TEST(L3LineParsing, AddLine) {
    auto path = write_temp_csv(
        "1704067200000000000,ADD,1,BUY,42150.50,10\n");
    L3FeedParser parser;
    ASSERT_TRUE(parser.open(path));

    L3Record record;
    ASSERT_TRUE(parser.next(record));
    EXPECT_TRUE(record.valid);
    EXPECT_EQ(record.timestamp, 1704067200000000000ULL);
    EXPECT_EQ(record.event_type, L3EventType::Add);
    EXPECT_EQ(record.order_id, 1u);
    EXPECT_EQ(record.side, Side::Buy);
    EXPECT_EQ(record.price, 42150LL * PRICE_SCALE + 50000000LL);
    EXPECT_EQ(record.quantity, 10u);

    parser.close();
    remove_temp_csv(path);
}

TEST(L3LineParsing, CancelLine) {
    auto path = write_temp_csv(
        "1704067200000200000,CANCEL,5,,,\n");
    L3FeedParser parser;
    ASSERT_TRUE(parser.open(path));

    L3Record record;
    ASSERT_TRUE(parser.next(record));
    EXPECT_TRUE(record.valid);
    EXPECT_EQ(record.event_type, L3EventType::Cancel);
    EXPECT_EQ(record.order_id, 5u);

    parser.close();
    remove_temp_csv(path);
}

TEST(L3LineParsing, TradeLine) {
    auto path = write_temp_csv(
        "1704067200000300000,TRADE,,BUY,42155.25,5\n");
    L3FeedParser parser;
    ASSERT_TRUE(parser.open(path));

    L3Record record;
    ASSERT_TRUE(parser.next(record));
    EXPECT_TRUE(record.valid);
    EXPECT_EQ(record.event_type, L3EventType::Trade);
    EXPECT_EQ(record.side, Side::Buy);
    EXPECT_EQ(record.price, 42155LL * PRICE_SCALE + 25000000LL);
    EXPECT_EQ(record.quantity, 5u);

    parser.close();
    remove_temp_csv(path);
}

TEST(L3LineParsing, HeaderSkipped) {
    auto path = write_temp_csv(
        "timestamp,event_type,order_id,side,price,quantity\n"
        "1704067200000000000,ADD,1,BUY,42000.00,10\n");
    L3FeedParser parser;
    ASSERT_TRUE(parser.open(path));

    L3Record record;
    ASSERT_TRUE(parser.next(record));
    EXPECT_TRUE(record.valid);
    EXPECT_EQ(record.event_type, L3EventType::Add);
    EXPECT_EQ(record.order_id, 1u);

    // No more records
    EXPECT_FALSE(parser.next(record));

    parser.close();
    remove_temp_csv(path);
}

TEST(L3LineParsing, MalformedLine) {
    auto path = write_temp_csv(
        "garbage data here\n");
    L3FeedParser parser;
    ASSERT_TRUE(parser.open(path));

    L3Record record;
    ASSERT_TRUE(parser.next(record));
    EXPECT_FALSE(record.valid);
    EXPECT_GT(record.error.size(), 0u);

    parser.close();
    remove_temp_csv(path);
}

TEST(L3LineParsing, MissingFields) {
    auto path = write_temp_csv(
        "1704067200000000000,ADD,1\n");
    L3FeedParser parser;
    ASSERT_TRUE(parser.open(path));

    L3Record record;
    ASSERT_TRUE(parser.next(record));
    EXPECT_FALSE(record.valid);

    parser.close();
    remove_temp_csv(path);
}

TEST(L3LineParsing, EmptyFile) {
    auto path = write_temp_csv("");
    L3FeedParser parser;
    ASSERT_TRUE(parser.open(path));

    L3Record record;
    EXPECT_FALSE(parser.next(record));

    parser.close();
    remove_temp_csv(path);
}

TEST(L3LineParsing, EmptyLines) {
    auto path = write_temp_csv(
        "\n\n1704067200000000000,ADD,1,BUY,42000.00,10\n\n");
    L3FeedParser parser;
    ASSERT_TRUE(parser.open(path));

    L3Record record;
    ASSERT_TRUE(parser.next(record));
    EXPECT_TRUE(record.valid);
    EXPECT_EQ(record.order_id, 1u);

    EXPECT_FALSE(parser.next(record));

    parser.close();
    remove_temp_csv(path);
}

TEST(L3LineParsing, InvalidEventType) {
    auto path = write_temp_csv(
        "1704067200000000000,MODIFY,1,BUY,42000.00,10\n");
    L3FeedParser parser;
    ASSERT_TRUE(parser.open(path));

    L3Record record;
    ASSERT_TRUE(parser.next(record));
    EXPECT_FALSE(record.valid);
    EXPECT_NE(record.error.find("invalid event type"), std::string::npos);

    parser.close();
    remove_temp_csv(path);
}

TEST(L3LineParsing, ZeroQuantityReturnsInvalid) {
    auto path = write_temp_csv(
        "1704067200000000000,ADD,1,BUY,42000.00,0\n");
    L3FeedParser parser;
    ASSERT_TRUE(parser.open(path));

    L3Record record;
    ASSERT_TRUE(parser.next(record));
    EXPECT_FALSE(record.valid);

    parser.close();
    remove_temp_csv(path);
}

TEST(L3LineParsing, MultipleRecords) {
    auto path = write_temp_csv(
        "1704067200000000000,ADD,1,BUY,42000.00,10\n"
        "1704067200000100000,ADD,2,SELL,42001.00,5\n"
        "1704067200000200000,CANCEL,1,,,\n");
    L3FeedParser parser;
    ASSERT_TRUE(parser.open(path));

    L3Record record;

    ASSERT_TRUE(parser.next(record));
    EXPECT_TRUE(record.valid);
    EXPECT_EQ(record.event_type, L3EventType::Add);
    EXPECT_EQ(record.order_id, 1u);

    ASSERT_TRUE(parser.next(record));
    EXPECT_TRUE(record.valid);
    EXPECT_EQ(record.event_type, L3EventType::Add);
    EXPECT_EQ(record.order_id, 2u);

    ASSERT_TRUE(parser.next(record));
    EXPECT_TRUE(record.valid);
    EXPECT_EQ(record.event_type, L3EventType::Cancel);
    EXPECT_EQ(record.order_id, 1u);

    EXPECT_FALSE(parser.next(record));

    EXPECT_EQ(parser.lines_read(), 3u);
    EXPECT_EQ(parser.parse_errors(), 0u);

    parser.close();
    remove_temp_csv(path);
}

TEST(L3LineParsing, ResetRestartsFromBeginning) {
    auto path = write_temp_csv(
        "1704067200000000000,ADD,1,BUY,42000.00,10\n");
    L3FeedParser parser;
    ASSERT_TRUE(parser.open(path));

    L3Record record;
    ASSERT_TRUE(parser.next(record));
    EXPECT_TRUE(record.valid);
    EXPECT_FALSE(parser.next(record));

    parser.reset();
    EXPECT_EQ(parser.lines_read(), 0u);

    ASSERT_TRUE(parser.next(record));
    EXPECT_TRUE(record.valid);
    EXPECT_EQ(record.order_id, 1u);

    parser.close();
    remove_temp_csv(path);
}

TEST(L3LineParsing, ParseErrorsCounted) {
    auto path = write_temp_csv(
        "1704067200000000000,ADD,1,BUY,42000.00,10\n"
        "bad_line\n"
        "1704067200000200000,ADD,2,SELL,42001.00,5\n"
        "another_bad\n");
    L3FeedParser parser;
    ASSERT_TRUE(parser.open(path));

    L3Record record;
    int valid_count = 0;
    int error_count = 0;
    while (parser.next(record)) {
        if (record.valid) ++valid_count;
        else ++error_count;
    }

    EXPECT_EQ(valid_count, 2);
    EXPECT_EQ(error_count, 2);
    EXPECT_EQ(parser.parse_errors(), 2u);

    parser.close();
    remove_temp_csv(path);
}

// ===========================================================================
// OrderMessage conversion tests
// ===========================================================================

TEST(L3ToOrderMessage, AddRecord) {
    L3Record record{};
    record.timestamp = 1704067200000000000ULL;
    record.event_type = L3EventType::Add;
    record.order_id = 42;
    record.side = Side::Sell;
    record.price = 42150LL * PRICE_SCALE;
    record.quantity = 25;
    record.valid = true;

    OrderMessage msg = L3FeedParser::to_order_message(record);

    EXPECT_EQ(msg.type, MessageType::Add);
    EXPECT_EQ(msg.order.order_id, 42u);
    EXPECT_EQ(msg.order.side, Side::Sell);
    EXPECT_EQ(msg.order.type, OrderType::Limit);
    EXPECT_EQ(msg.order.time_in_force, TimeInForce::GTC);
    EXPECT_EQ(msg.order.price, 42150LL * PRICE_SCALE);
    EXPECT_EQ(msg.order.quantity, 25u);
    EXPECT_EQ(msg.order.visible_quantity, 25u);
    EXPECT_EQ(msg.order.filled_quantity, 0u);
    EXPECT_EQ(msg.order.timestamp, 1704067200000000000ULL);
    EXPECT_EQ(msg.order.next, nullptr);
    EXPECT_EQ(msg.order.prev, nullptr);
}

// ===========================================================================
// File I/O tests
// ===========================================================================

TEST(L3FileIO, OpenNonexistent) {
    L3FeedParser parser;
    EXPECT_FALSE(parser.open("nonexistent_file_xyz.csv"));
}

TEST(L3FileIO, OpenAndClose) {
    auto path = write_temp_csv(
        "1704067200000000000,ADD,1,BUY,42000.00,10\n");
    L3FeedParser parser;
    ASSERT_TRUE(parser.open(path));
    parser.close();

    // After close, next() returns false
    L3Record record;
    EXPECT_FALSE(parser.next(record));

    remove_temp_csv(path);
}

// ===========================================================================
// Cancel line with extra fields (as generated by our data script)
// ===========================================================================

TEST(L3LineParsing, CancelWithExtraFields) {
    auto path = write_temp_csv(
        "1704067200050200000,CANCEL,239,,41987.00,15\n");
    L3FeedParser parser;
    ASSERT_TRUE(parser.open(path));

    L3Record record;
    ASSERT_TRUE(parser.next(record));
    EXPECT_TRUE(record.valid);
    EXPECT_EQ(record.event_type, L3EventType::Cancel);
    EXPECT_EQ(record.order_id, 239u);

    parser.close();
    remove_temp_csv(path);
}

// ===========================================================================
// ReplayEngine integration tests
// ===========================================================================

class ReplayEngineTest : public ::testing::Test {
protected:
    ReplayConfig make_config(const std::string& csv_content) {
        temp_path_ = write_temp_csv(csv_content);
        ReplayConfig config;
        config.input_path = temp_path_;
        config.min_price = 41000LL * PRICE_SCALE;
        config.max_price = 43000LL * PRICE_SCALE;
        config.tick_size = PRICE_SCALE / 100;  // $0.01
        config.max_orders = 10000;
        return config;
    }

    void TearDown() override {
        if (!temp_path_.empty()) {
            remove_temp_csv(temp_path_);
        }
        // Clean up any generated report
        std::remove("test_report.json");
    }

    std::string temp_path_;
};

TEST_F(ReplayEngineTest, EmptyFile) {
    auto config = make_config("");
    ReplayEngine engine(config);
    auto stats = engine.run();

    EXPECT_EQ(stats.total_messages, 0u);
    EXPECT_EQ(stats.add_messages, 0u);
    EXPECT_EQ(stats.final_order_count, 0u);
}

TEST_F(ReplayEngineTest, SingleAdd) {
    auto config = make_config(
        "1704067200000000000,ADD,1,BUY,42000.00,10\n");
    ReplayEngine engine(config);
    auto stats = engine.run();

    EXPECT_EQ(stats.total_messages, 1u);
    EXPECT_EQ(stats.add_messages, 1u);
    EXPECT_EQ(stats.orders_accepted, 1u);
    EXPECT_EQ(stats.orders_rejected, 0u);
    EXPECT_EQ(stats.final_order_count, 1u);
    EXPECT_EQ(stats.final_best_bid, 42000LL * PRICE_SCALE);
}

TEST_F(ReplayEngineTest, BuyAndSellNoMatch) {
    auto config = make_config(
        "1704067200000000000,ADD,1,BUY,42000.00,10\n"
        "1704067200000100000,ADD,2,SELL,42001.00,5\n");
    ReplayEngine engine(config);
    auto stats = engine.run();

    EXPECT_EQ(stats.total_messages, 2u);
    EXPECT_EQ(stats.orders_accepted, 2u);
    EXPECT_EQ(stats.trades_generated, 0u);
    EXPECT_EQ(stats.final_order_count, 2u);
    EXPECT_EQ(stats.final_best_bid, 42000LL * PRICE_SCALE);
    EXPECT_EQ(stats.final_best_ask, 42001LL * PRICE_SCALE);
    EXPECT_EQ(stats.final_spread, 1LL * PRICE_SCALE);
}

TEST_F(ReplayEngineTest, CrossingOrders) {
    auto config = make_config(
        "1704067200000000000,ADD,1,SELL,42000.00,10\n"
        "1704067200000100000,ADD,2,BUY,42000.00,10\n");
    ReplayEngine engine(config);
    auto stats = engine.run();

    EXPECT_EQ(stats.orders_accepted, 2u);
    EXPECT_EQ(stats.trades_generated, 1u);
    EXPECT_EQ(stats.final_order_count, 0u);
}

TEST_F(ReplayEngineTest, PartialFillCross) {
    auto config = make_config(
        "1704067200000000000,ADD,1,SELL,42000.00,10\n"
        "1704067200000100000,ADD,2,BUY,42000.00,5\n");
    ReplayEngine engine(config);
    auto stats = engine.run();

    EXPECT_EQ(stats.orders_accepted, 2u);
    EXPECT_EQ(stats.trades_generated, 1u);
    // Sell order has 5 remaining
    EXPECT_EQ(stats.final_order_count, 1u);
    EXPECT_EQ(stats.final_best_ask, 42000LL * PRICE_SCALE);
}

TEST_F(ReplayEngineTest, CancelOrder) {
    auto config = make_config(
        "1704067200000000000,ADD,1,BUY,42000.00,10\n"
        "1704067200000100000,CANCEL,1,,,\n");
    ReplayEngine engine(config);
    auto stats = engine.run();

    EXPECT_EQ(stats.add_messages, 1u);
    EXPECT_EQ(stats.cancel_messages, 1u);
    EXPECT_EQ(stats.orders_accepted, 1u);
    EXPECT_EQ(stats.orders_cancelled, 1u);
    EXPECT_EQ(stats.final_order_count, 0u);
}

TEST_F(ReplayEngineTest, CancelNonexistentOrder) {
    auto config = make_config(
        "1704067200000000000,ADD,1,BUY,42000.00,10\n"
        "1704067200000100000,CANCEL,999,,,\n");
    ReplayEngine engine(config);
    auto stats = engine.run();

    EXPECT_EQ(stats.cancel_messages, 1u);
    EXPECT_EQ(stats.cancel_failures, 1u);
    EXPECT_EQ(stats.orders_cancelled, 0u);
    EXPECT_EQ(stats.final_order_count, 1u);
}

TEST_F(ReplayEngineTest, TradeRecordIsInformational) {
    auto config = make_config(
        "1704067200000000000,TRADE,,BUY,42000.00,10\n");
    ReplayEngine engine(config);
    auto stats = engine.run();

    EXPECT_EQ(stats.total_messages, 1u);
    EXPECT_EQ(stats.trade_messages, 1u);
    EXPECT_EQ(stats.add_messages, 0u);
    // No actual orders placed
    EXPECT_EQ(stats.orders_accepted, 0u);
    EXPECT_EQ(stats.final_order_count, 0u);
}

TEST_F(ReplayEngineTest, MultiLevelSweep) {
    auto config = make_config(
        "1704067200000000000,ADD,1,SELL,42000.00,5\n"
        "1704067200000100000,ADD,2,SELL,42001.00,5\n"
        "1704067200000200000,ADD,3,SELL,42002.00,5\n"
        "1704067200000300000,ADD,4,BUY,42002.00,15\n");
    ReplayEngine engine(config);
    auto stats = engine.run();

    EXPECT_EQ(stats.orders_accepted, 4u);
    EXPECT_EQ(stats.trades_generated, 3u);
    EXPECT_EQ(stats.final_order_count, 0u);
}

TEST_F(ReplayEngineTest, ParseErrorsCounted) {
    auto config = make_config(
        "1704067200000000000,ADD,1,BUY,42000.00,10\n"
        "bad_line_here\n"
        "1704067200000200000,ADD,2,SELL,42001.00,5\n");
    ReplayEngine engine(config);
    auto stats = engine.run();

    EXPECT_EQ(stats.total_messages, 3u);
    EXPECT_EQ(stats.parse_errors, 1u);
    EXPECT_EQ(stats.orders_accepted, 2u);
}

TEST_F(ReplayEngineTest, OutOfRangePriceRejected) {
    auto config = make_config(
        "1704067200000000000,ADD,1,BUY,50000.00,10\n");
    ReplayEngine engine(config);
    auto stats = engine.run();

    // Price $50,000 is outside the $41,000-$43,000 range.
    // The gateway accepts the order (passes gateway validation) but the
    // matching engine rejects it (is_valid_price fails). So it counts as
    // "accepted" by the gateway but results in MatchStatus::Rejected.
    EXPECT_EQ(stats.add_messages, 1u);
    EXPECT_EQ(stats.orders_accepted, 1u);
    EXPECT_EQ(stats.trades_generated, 0u);
    EXPECT_EQ(stats.final_order_count, 0u);
}

TEST_F(ReplayEngineTest, JsonReportWritten) {
    auto config = make_config(
        "1704067200000000000,ADD,1,BUY,42000.00,10\n");
    config.output_path = "test_report.json";

    ReplayEngine engine(config);
    auto stats = engine.run();

    // Verify report file exists
    std::ifstream report("test_report.json");
    EXPECT_TRUE(report.is_open());
    std::string content((std::istreambuf_iterator<char>(report)),
                        std::istreambuf_iterator<char>());
    EXPECT_GT(content.size(), 0u);
    // Check it contains expected JSON keys
    EXPECT_NE(content.find("input_file"), std::string::npos);
    EXPECT_NE(content.find("messages"), std::string::npos);
    EXPECT_NE(content.find("final_state"), std::string::npos);
    EXPECT_NE(content.find("performance"), std::string::npos);
}

TEST_F(ReplayEngineTest, EventCallbackFires) {
    auto config = make_config(
        "1704067200000000000,ADD,1,SELL,42000.00,10\n"
        "1704067200000100000,ADD,2,BUY,42000.00,10\n");
    config.enable_publisher = true;

    ReplayEngine engine(config);
    std::vector<EventMessage> events;
    engine.register_event_callback([&](const EventMessage& e) {
        events.push_back(e);
    });

    auto stats = engine.run();

    // Should have received events: OrderAccepted for sell, Trade + OrderFilled for buy
    EXPECT_GE(events.size(), 3u);
}

TEST_F(ReplayEngineTest, HeaderLineSkipped) {
    auto config = make_config(
        "timestamp,event_type,order_id,side,price,quantity\n"
        "1704067200000000000,ADD,1,BUY,42000.00,10\n");
    ReplayEngine engine(config);
    auto stats = engine.run();

    EXPECT_EQ(stats.total_messages, 1u);
    EXPECT_EQ(stats.orders_accepted, 1u);
}

TEST_F(ReplayEngineTest, BuildBookAndVerifyState) {
    auto config = make_config(
        "1704067200000000000,ADD,1,BUY,42000.00,10\n"
        "1704067200000100000,ADD,2,BUY,41999.00,20\n"
        "1704067200000200000,ADD,3,BUY,41998.00,30\n"
        "1704067200000300000,ADD,4,SELL,42001.00,15\n"
        "1704067200000400000,ADD,5,SELL,42002.00,25\n"
        "1704067200000500000,ADD,6,SELL,42003.00,35\n");
    ReplayEngine engine(config);
    auto stats = engine.run();

    EXPECT_EQ(stats.final_order_count, 6u);
    EXPECT_EQ(stats.final_best_bid, 42000LL * PRICE_SCALE);
    EXPECT_EQ(stats.final_best_ask, 42001LL * PRICE_SCALE);
    EXPECT_EQ(stats.final_spread, 1LL * PRICE_SCALE);

    // Verify book state via accessor
    const auto& book = engine.order_book();
    EXPECT_EQ(book.order_count(), 6u);
    EXPECT_EQ(book.best_bid()->price, 42000LL * PRICE_SCALE);
    EXPECT_EQ(book.best_ask()->price, 42001LL * PRICE_SCALE);
}

// ===========================================================================
// End-to-end: Replay the full sample CSV
// ===========================================================================

TEST(L3EndToEnd, ReplaySampleCSV) {
    // This test replays the full data/btcusdt_l3_sample.csv file
    ReplayConfig config;
    config.input_path = "data/btcusdt_l3_sample.csv";

    // Try alternate paths for running from different directories
    std::ifstream probe(config.input_path);
    if (!probe.is_open()) {
        config.input_path = "../data/btcusdt_l3_sample.csv";
        probe.open(config.input_path);
    }
    if (!probe.is_open()) {
        config.input_path = "../../data/btcusdt_l3_sample.csv";
        probe.open(config.input_path);
    }
    probe.close();

    config.min_price = 41000LL * PRICE_SCALE;
    config.max_price = 43000LL * PRICE_SCALE;
    config.tick_size = PRICE_SCALE / 100;  // $0.01
    config.max_orders = 100000;

    ReplayEngine engine(config);
    ReplayStats stats = engine.run();

    // Verify basic expectations from the 5K-message sample file
    EXPECT_GT(stats.total_messages, 4000u);
    EXPECT_EQ(stats.parse_errors, 0u);

    // Messages breakdown
    EXPECT_GT(stats.add_messages, 2000u);
    EXPECT_GT(stats.cancel_messages, 500u);
    EXPECT_GT(stats.trade_messages, 100u);

    // Some orders should have matched (crossing orders in the data)
    EXPECT_GT(stats.trades_generated, 0u);

    // Book should have resting orders on both sides
    EXPECT_GT(stats.final_order_count, 0u);
    EXPECT_GT(stats.final_best_bid, 0);
    EXPECT_GT(stats.final_best_ask, 0);
    EXPECT_GT(stats.final_spread, 0);

    // Performance: should process all messages in well under 1 second
    EXPECT_LT(stats.elapsed_seconds, 5.0);
    EXPECT_GT(stats.messages_per_second, 10000.0);
}
