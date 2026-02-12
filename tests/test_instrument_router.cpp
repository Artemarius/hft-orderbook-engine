/// @file test_instrument_router.cpp
/// @brief Unit tests for InstrumentRegistry and InstrumentRouter.

#include <vector>

#include <gtest/gtest.h>

#include "core/order.h"
#include "core/types.h"
#include "gateway/instrument_registry.h"
#include "gateway/instrument_router.h"
#include "transport/event_buffer.h"
#include "transport/message.h"

using namespace hft;

// ===========================================================================
// Helpers
// ===========================================================================

static OrderMessage make_msg(InstrumentId inst, OrderId id, Side side,
                              Price price, Quantity qty,
                              MessageType type = MessageType::Add) {
    OrderMessage msg{};
    msg.type = type;
    msg.instrument_id = inst;
    msg.order.order_id = id;
    msg.order.participant_id = 1;
    msg.order.instrument_id = inst;
    msg.order.side = side;
    msg.order.type = OrderType::Limit;
    msg.order.time_in_force = TimeInForce::GTC;
    msg.order.status = OrderStatus::New;
    msg.order.price = price;
    msg.order.quantity = qty;
    msg.order.visible_quantity = qty;
    msg.order.iceberg_slice_qty = 0;
    msg.order.filled_quantity = 0;
    msg.order.timestamp = 1000;
    msg.order.next = nullptr;
    msg.order.prev = nullptr;
    return msg;
}

static std::vector<EventMessage> drain(EventBuffer& buf) {
    std::vector<EventMessage> events;
    EventMessage e{};
    while (buf.try_pop(e)) {
        events.push_back(e);
    }
    return events;
}

// ===========================================================================
// InstrumentRegistry tests
// ===========================================================================

TEST(InstrumentRegistryTest, RegisterAndFind) {
    InstrumentRegistry reg;
    InstrumentConfig cfg;
    cfg.instrument_id = 1;
    cfg.symbol = "BTCUSDT";
    cfg.min_price = 40000 * PRICE_SCALE;
    cfg.max_price = 60000 * PRICE_SCALE;
    cfg.tick_size = PRICE_SCALE / 100;
    cfg.max_orders = 1000;

    EXPECT_TRUE(reg.register_instrument(cfg));
    EXPECT_EQ(reg.count(), 1u);

    auto* found_id = reg.find_by_id(1);
    ASSERT_NE(found_id, nullptr);
    EXPECT_EQ(found_id->symbol, "BTCUSDT");

    auto* found_sym = reg.find_by_symbol("BTCUSDT");
    ASSERT_NE(found_sym, nullptr);
    EXPECT_EQ(found_sym->instrument_id, 1u);
}

TEST(InstrumentRegistryTest, RejectDuplicateId) {
    InstrumentRegistry reg;
    InstrumentConfig cfg1;
    cfg1.instrument_id = 1;
    cfg1.symbol = "BTCUSDT";
    cfg1.min_price = 1;
    cfg1.max_price = 100;
    cfg1.tick_size = 1;

    InstrumentConfig cfg2;
    cfg2.instrument_id = 1;  // duplicate ID
    cfg2.symbol = "ETHUSDT";
    cfg2.min_price = 1;
    cfg2.max_price = 100;
    cfg2.tick_size = 1;

    EXPECT_TRUE(reg.register_instrument(cfg1));
    EXPECT_FALSE(reg.register_instrument(cfg2));
    EXPECT_EQ(reg.count(), 1u);
}

TEST(InstrumentRegistryTest, RejectDuplicateSymbol) {
    InstrumentRegistry reg;
    InstrumentConfig cfg1;
    cfg1.instrument_id = 1;
    cfg1.symbol = "BTCUSDT";
    cfg1.min_price = 1;
    cfg1.max_price = 100;
    cfg1.tick_size = 1;

    InstrumentConfig cfg2;
    cfg2.instrument_id = 2;
    cfg2.symbol = "BTCUSDT";  // duplicate symbol
    cfg2.min_price = 1;
    cfg2.max_price = 100;
    cfg2.tick_size = 1;

    EXPECT_TRUE(reg.register_instrument(cfg1));
    EXPECT_FALSE(reg.register_instrument(cfg2));
}

TEST(InstrumentRegistryTest, FindUnknownReturnsNull) {
    InstrumentRegistry reg;
    EXPECT_EQ(reg.find_by_id(42), nullptr);
    EXPECT_EQ(reg.find_by_symbol("UNKNOWN"), nullptr);
}

// ===========================================================================
// InstrumentRouter fixture
// ===========================================================================

class InstrumentRouterTest : public ::testing::Test {
protected:
    void SetUp() override {
        InstrumentConfig btc;
        btc.instrument_id = 0;
        btc.symbol = "BTCUSDT";
        btc.min_price = 1 * PRICE_SCALE;
        btc.max_price = 1000 * PRICE_SCALE;
        btc.tick_size = 1 * PRICE_SCALE;
        btc.max_orders = 10000;

        InstrumentConfig eth;
        eth.instrument_id = 1;
        eth.symbol = "ETHUSDT";
        eth.min_price = 1 * PRICE_SCALE;
        eth.max_price = 1000 * PRICE_SCALE;
        eth.tick_size = 1 * PRICE_SCALE;
        eth.max_orders = 10000;

        registry.register_instrument(btc);
        registry.register_instrument(eth);

        buffer = std::make_unique<EventBuffer>();
        router = std::make_unique<InstrumentRouter>(registry, buffer.get());
    }

    InstrumentRegistry registry;
    std::unique_ptr<EventBuffer> buffer;
    std::unique_ptr<InstrumentRouter> router;
};

// ===========================================================================
// Router tests
// ===========================================================================

TEST_F(InstrumentRouterTest, InstrumentCount) {
    EXPECT_EQ(router->instrument_count(), 2u);
}

TEST_F(InstrumentRouterTest, SingleInstrumentRouting) {
    auto msg = make_msg(0, 1, Side::Buy, 100 * PRICE_SCALE, 10);
    auto result = router->process_order(msg);

    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.match_status, MatchStatus::Resting);

    const OrderBook* btc_book = router->order_book(0);
    ASSERT_NE(btc_book, nullptr);
    EXPECT_EQ(btc_book->order_count(), 1u);

    const OrderBook* eth_book = router->order_book(1);
    ASSERT_NE(eth_book, nullptr);
    EXPECT_EQ(eth_book->order_count(), 0u);
}

TEST_F(InstrumentRouterTest, TwoInstrumentsNoInterference) {
    // Buy on BTC
    auto btc_buy = make_msg(0, 1, Side::Buy, 100 * PRICE_SCALE, 10);
    EXPECT_TRUE(router->process_order(btc_buy).accepted);

    // Sell on ETH at same price — must NOT match BTC buy
    auto eth_sell = make_msg(1, 2, Side::Sell, 100 * PRICE_SCALE, 10);
    auto eth_result = router->process_order(eth_sell);
    EXPECT_TRUE(eth_result.accepted);
    EXPECT_EQ(eth_result.match_status, MatchStatus::Resting);  // no cross

    EXPECT_EQ(router->order_book(0)->order_count(), 1u);
    EXPECT_EQ(router->order_book(1)->order_count(), 1u);
}

TEST_F(InstrumentRouterTest, CancelRoutedCorrectly) {
    auto msg = make_msg(1, 1, Side::Buy, 50 * PRICE_SCALE, 10);
    (void)router->process_order(msg);

    // Cancel on wrong instrument fails
    EXPECT_FALSE(router->process_cancel(0, 1));

    // Cancel on correct instrument succeeds
    EXPECT_TRUE(router->process_cancel(1, 1));
    EXPECT_EQ(router->order_book(1)->order_count(), 0u);
}

TEST_F(InstrumentRouterTest, ModifyRoutedCorrectly) {
    auto msg = make_msg(0, 1, Side::Buy, 50 * PRICE_SCALE, 10);
    (void)router->process_order(msg);
    drain(*buffer);

    // Modify on correct instrument
    auto mod = make_msg(0, 1, Side::Buy, 60 * PRICE_SCALE, 10, MessageType::Modify);
    auto result = router->process_modify(mod);
    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.match_status, MatchStatus::Modified);
    EXPECT_EQ(router->order_book(0)->best_bid()->price, 60 * PRICE_SCALE);
}

TEST_F(InstrumentRouterTest, UnknownInstrumentRejected) {
    auto msg = make_msg(99, 1, Side::Buy, 100 * PRICE_SCALE, 10);
    auto result = router->process_order(msg);
    EXPECT_FALSE(result.accepted);

    EXPECT_FALSE(router->process_cancel(99, 1));

    auto mod = make_msg(99, 1, Side::Buy, 100 * PRICE_SCALE, 10, MessageType::Modify);
    auto mod_result = router->process_modify(mod);
    EXPECT_FALSE(mod_result.accepted);
}

TEST_F(InstrumentRouterTest, EventsCarryInstrumentId) {
    auto msg = make_msg(1, 1, Side::Buy, 100 * PRICE_SCALE, 10);
    (void)router->process_order(msg);

    auto events = drain(*buffer);
    ASSERT_GE(events.size(), 1u);
    EXPECT_EQ(events[0].instrument_id, 1u);
}

TEST_F(InstrumentRouterTest, SharedEventBuffer) {
    // Order on BTC
    auto btc_msg = make_msg(0, 1, Side::Buy, 100 * PRICE_SCALE, 10);
    (void)router->process_order(btc_msg);

    // Order on ETH
    auto eth_msg = make_msg(1, 2, Side::Sell, 200 * PRICE_SCALE, 5);
    (void)router->process_order(eth_msg);

    auto events = drain(*buffer);
    ASSERT_EQ(events.size(), 2u);

    // Both events in same buffer, tagged with their instrument_id
    EXPECT_EQ(events[0].instrument_id, 0u);
    EXPECT_EQ(events[1].instrument_id, 1u);
}

TEST_F(InstrumentRouterTest, PerInstrumentOrderIsolation) {
    // 5 orders on BTC, 3 on ETH
    for (OrderId i = 1; i <= 5; ++i) {
        auto msg = make_msg(0, i, Side::Buy,
                             static_cast<Price>((100 - i) * PRICE_SCALE), 10);
        (void)router->process_order(msg);
    }
    for (OrderId i = 10; i <= 12; ++i) {
        auto msg = make_msg(1, i, Side::Sell,
                             static_cast<Price>((200 + i) * PRICE_SCALE), 5);
        (void)router->process_order(msg);
    }

    EXPECT_EQ(router->order_book(0)->order_count(), 5u);
    EXPECT_EQ(router->order_book(1)->order_count(), 3u);
}

TEST_F(InstrumentRouterTest, PipelineAccessor) {
    const InstrumentPipeline* p = router->pipeline(0);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->instrument_id, 0u);
    EXPECT_NE(p->book, nullptr);
    EXPECT_NE(p->engine, nullptr);
    EXPECT_NE(p->gateway, nullptr);
    EXPECT_NE(p->pool, nullptr);

    EXPECT_EQ(router->pipeline(99), nullptr);
}

TEST_F(InstrumentRouterTest, OrderBookAccessor) {
    EXPECT_NE(router->order_book(0), nullptr);
    EXPECT_NE(router->order_book(1), nullptr);
    EXPECT_EQ(router->order_book(99), nullptr);
}
