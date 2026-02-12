/// @file test_multi_instrument_replay.cpp
/// @brief Unit tests for multi-instrument CSV parsing and replay.

#include <cstdio>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "analytics/multi_instrument_analytics.h"
#include "core/types.h"
#include "feed/l3_feed_parser.h"
#include "feed/multi_instrument_replay_engine.h"
#include "transport/message.h"

using namespace hft;

// ===========================================================================
// Helper: write a temp CSV file and return its path
// ===========================================================================

static std::string write_temp_csv(const std::string& content,
                                   const std::string& suffix = ".csv") {
    std::string path = "test_multi_inst_" + suffix;
    std::ofstream out(path);
    out << content;
    out.close();
    return path;
}

// ===========================================================================
// L3FeedParser: 7-column format
// ===========================================================================

TEST(MultiInstrumentParsing, SevenColumnFormat) {
    std::string csv =
        "symbol,timestamp,event_type,order_id,side,price,quantity\n"
        "BTCUSDT,1000000,ADD,1,BUY,42000.50,100\n"
        "ETHUSDT,1000001,ADD,2,SELL,3200.75,50\n"
        "BTCUSDT,1000002,CANCEL,1,,,\n";

    auto path = write_temp_csv(csv, "7col.csv");

    L3FeedParser parser;
    ASSERT_TRUE(parser.open(path));

    L3Record record;

    // First record: BTC ADD
    ASSERT_TRUE(parser.next(record));
    EXPECT_TRUE(record.valid);
    EXPECT_EQ(record.symbol, "BTCUSDT");
    EXPECT_EQ(record.event_type, L3EventType::Add);
    EXPECT_EQ(record.order_id, 1u);
    EXPECT_EQ(record.side, Side::Buy);
    EXPECT_EQ(record.price, 4200050000000LL);  // 42000.50 * PRICE_SCALE
    EXPECT_EQ(record.quantity, 100u);
    EXPECT_TRUE(parser.has_symbol_column());

    // Second record: ETH ADD
    ASSERT_TRUE(parser.next(record));
    EXPECT_TRUE(record.valid);
    EXPECT_EQ(record.symbol, "ETHUSDT");
    EXPECT_EQ(record.event_type, L3EventType::Add);
    EXPECT_EQ(record.order_id, 2u);
    EXPECT_EQ(record.side, Side::Sell);

    // Third record: BTC CANCEL
    ASSERT_TRUE(parser.next(record));
    EXPECT_TRUE(record.valid);
    EXPECT_EQ(record.symbol, "BTCUSDT");
    EXPECT_EQ(record.event_type, L3EventType::Cancel);
    EXPECT_EQ(record.order_id, 1u);

    // EOF
    EXPECT_FALSE(parser.next(record));

    parser.close();
    std::remove(path.c_str());
}

TEST(MultiInstrumentParsing, SixColumnBackwardCompat) {
    std::string csv =
        "timestamp,event_type,order_id,side,price,quantity\n"
        "1000000,ADD,1,BUY,42000.50,100\n";

    auto path = write_temp_csv(csv, "6col.csv");

    L3FeedParser parser;
    ASSERT_TRUE(parser.open(path));

    L3Record record;
    ASSERT_TRUE(parser.next(record));
    EXPECT_TRUE(record.valid);
    EXPECT_TRUE(record.symbol.empty());
    EXPECT_EQ(record.event_type, L3EventType::Add);
    EXPECT_EQ(record.order_id, 1u);
    EXPECT_FALSE(parser.has_symbol_column());

    parser.close();
    std::remove(path.c_str());
}

TEST(MultiInstrumentParsing, ToOrderMessageWithInstrumentId) {
    L3Record record;
    record.timestamp = 1000;
    record.event_type = L3EventType::Add;
    record.order_id = 42;
    record.side = Side::Buy;
    record.price = 100 * PRICE_SCALE;
    record.quantity = 10;
    record.valid = true;

    OrderMessage msg = L3FeedParser::to_order_message(record, 5);
    EXPECT_EQ(msg.instrument_id, 5u);
    EXPECT_EQ(msg.order.instrument_id, 5u);
    EXPECT_EQ(msg.order.order_id, 42u);
}

// ===========================================================================
// MultiInstrumentReplayEngine
// ===========================================================================

TEST(MultiInstrumentReplay, TwoInstrumentReplay) {
    // Write a multi-instrument CSV
    std::string csv =
        "symbol,timestamp,event_type,order_id,side,price,quantity\n"
        "BTCUSDT,1000000,ADD,1,BUY,100.00,10\n"
        "ETHUSDT,1000001,ADD,2,SELL,200.00,5\n"
        "BTCUSDT,1000002,ADD,3,SELL,100.00,10\n"  // crosses BTC buy
        "ETHUSDT,1000003,ADD,4,BUY,200.00,5\n"    // crosses ETH sell
        "BTCUSDT,1000004,CANCEL,3,,,\n";           // cancel BTC order (already filled)

    auto path = write_temp_csv(csv, "multi_replay.csv");

    MultiReplayConfig config;
    config.input_path = path;
    config.auto_discover = true;
    config.default_min_price = 1 * PRICE_SCALE;
    config.default_max_price = 1000 * PRICE_SCALE;
    config.default_tick_size = 1 * PRICE_SCALE;
    config.default_max_orders = 1000;

    MultiInstrumentReplayEngine engine(config);
    MultiReplayStats stats = engine.run();

    EXPECT_EQ(stats.total_messages, 5u);
    EXPECT_EQ(stats.per_instrument.size(), 2u);

    // Find BTC and ETH stats
    const PerInstrumentStats* btc = nullptr;
    const PerInstrumentStats* eth = nullptr;
    for (const auto& ps : stats.per_instrument) {
        if (ps.symbol == "BTCUSDT") btc = &ps;
        if (ps.symbol == "ETHUSDT") eth = &ps;
    }

    ASSERT_NE(btc, nullptr);
    ASSERT_NE(eth, nullptr);

    EXPECT_EQ(btc->add_messages, 2u);
    EXPECT_EQ(eth->add_messages, 2u);
    EXPECT_EQ(btc->cancel_messages, 1u);

    std::remove(path.c_str());
}

TEST(MultiInstrumentReplay, PerInstrumentAnalyticsIsolation) {
    std::string csv =
        "symbol,timestamp,event_type,order_id,side,price,quantity\n"
        "BTCUSDT,1000000,ADD,1,BUY,100.00,10\n"
        "ETHUSDT,1000001,ADD,2,SELL,200.00,5\n";

    auto path = write_temp_csv(csv, "analytics_isolation.csv");

    // Use pre-configured instruments so router is available immediately
    InstrumentConfig btc;
    btc.instrument_id = 0;
    btc.symbol = "BTCUSDT";
    btc.min_price = 1 * PRICE_SCALE;
    btc.max_price = 1000 * PRICE_SCALE;
    btc.tick_size = 1 * PRICE_SCALE;
    btc.max_orders = 1000;

    InstrumentConfig eth;
    eth.instrument_id = 1;
    eth.symbol = "ETHUSDT";
    eth.min_price = 1 * PRICE_SCALE;
    eth.max_price = 1000 * PRICE_SCALE;
    eth.tick_size = 1 * PRICE_SCALE;
    eth.max_orders = 1000;

    MultiReplayConfig config;
    config.input_path = path;
    config.instruments = {btc, eth};

    MultiInstrumentReplayEngine engine(config);

    MultiInstrumentAnalytics analytics(engine.router());
    engine.register_event_callback(
        [&analytics](const EventMessage& event) {
            analytics.on_event(event);
        });

    (void)engine.run();

    // Both instruments should have analytics engines
    EXPECT_NE(analytics.analytics(0), nullptr);
    EXPECT_NE(analytics.analytics(1), nullptr);

    std::remove(path.c_str());
}

TEST(MultiInstrumentReplay, PreConfiguredInstruments) {
    std::string csv =
        "symbol,timestamp,event_type,order_id,side,price,quantity\n"
        "BTCUSDT,1000000,ADD,1,BUY,100.00,10\n"
        "ETHUSDT,1000001,ADD,2,SELL,200.00,5\n";

    auto path = write_temp_csv(csv, "preconfigured.csv");

    InstrumentConfig btc;
    btc.instrument_id = 0;
    btc.symbol = "BTCUSDT";
    btc.min_price = 1 * PRICE_SCALE;
    btc.max_price = 1000 * PRICE_SCALE;
    btc.tick_size = 1 * PRICE_SCALE;
    btc.max_orders = 1000;

    InstrumentConfig eth;
    eth.instrument_id = 1;
    eth.symbol = "ETHUSDT";
    eth.min_price = 1 * PRICE_SCALE;
    eth.max_price = 1000 * PRICE_SCALE;
    eth.tick_size = 1 * PRICE_SCALE;
    eth.max_orders = 1000;

    MultiReplayConfig config;
    config.input_path = path;
    config.instruments = {btc, eth};

    MultiInstrumentReplayEngine engine(config);
    MultiReplayStats stats = engine.run();

    EXPECT_EQ(stats.total_messages, 2u);
    EXPECT_EQ(stats.per_instrument.size(), 2u);

    // BTC order should be on instrument 0's book
    const OrderBook* btc_book = engine.router().order_book(0);
    ASSERT_NE(btc_book, nullptr);
    EXPECT_EQ(btc_book->order_count(), 1u);

    // ETH order on instrument 1's book
    const OrderBook* eth_book = engine.router().order_book(1);
    ASSERT_NE(eth_book, nullptr);
    EXPECT_EQ(eth_book->order_count(), 1u);

    std::remove(path.c_str());
}
