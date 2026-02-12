/// @file test_gateway.cpp
/// @brief Unit tests for OrderGateway, MarketDataPublisher, and the
///        end-to-end event pipeline (Phase 5).

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "core/order.h"
#include "core/types.h"
#include "gateway/market_data_publisher.h"
#include "gateway/order_gateway.h"
#include "matching/match_result.h"
#include "matching/matching_engine.h"
#include "orderbook/memory_pool.h"
#include "orderbook/order_book.h"
#include "transport/event_buffer.h"
#include "transport/message.h"

using namespace hft;

// ===========================================================================
// Helpers
// ===========================================================================

static OrderMessage make_order_msg(OrderId id, Side side, OrderType type,
                                   Price price, Quantity qty,
                                   ParticipantId participant = 1) {
    OrderMessage msg{};
    msg.type = MessageType::Add;
    msg.order.order_id = id;
    msg.order.participant_id = participant;
    msg.order.side = side;
    msg.order.type = type;
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

static std::vector<EventMessage> drain_events(EventBuffer& buffer) {
    std::vector<EventMessage> events;
    EventMessage event{};
    while (buffer.try_pop(event)) {
        events.push_back(event);
    }
    return events;
}

// ===========================================================================
// Test fixture
// ===========================================================================

class GatewayTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Price range: 1-1000 (fixed-point), tick size = 1
        book = std::make_unique<OrderBook>(
            1 * PRICE_SCALE, 1000 * PRICE_SCALE, 1 * PRICE_SCALE, 10000);
        pool = std::make_unique<MemoryPool<Order>>(10000);
        engine = std::make_unique<MatchingEngine>(*book, *pool);
        buffer = std::make_unique<EventBuffer>();
        gateway = std::make_unique<OrderGateway>(*engine, *pool, buffer.get());
    }

    std::unique_ptr<OrderBook> book;
    std::unique_ptr<MemoryPool<Order>> pool;
    std::unique_ptr<MatchingEngine> engine;
    std::unique_ptr<EventBuffer> buffer;
    std::unique_ptr<OrderGateway> gateway;
};

// ===========================================================================
// Validation tests
// ===========================================================================

TEST_F(GatewayTest, RejectZeroQuantity) {
    auto msg = make_order_msg(1, Side::Buy, OrderType::Limit,
                              100 * PRICE_SCALE, 0);
    auto result = gateway->process_order(msg);

    EXPECT_FALSE(result.accepted);
    EXPECT_EQ(result.reject_reason, GatewayRejectReason::InvalidQuantity);
    EXPECT_EQ(result.match_status, MatchStatus::Rejected);

    auto events = drain_events(*buffer);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, EventType::OrderRejected);
    EXPECT_EQ(events[0].data.order_event.order_id, 1u);
}

TEST_F(GatewayTest, RejectNegativePrice) {
    auto msg = make_order_msg(1, Side::Buy, OrderType::Limit,
                              -5 * PRICE_SCALE, 10);
    auto result = gateway->process_order(msg);

    EXPECT_FALSE(result.accepted);
    EXPECT_EQ(result.reject_reason, GatewayRejectReason::InvalidPrice);

    auto events = drain_events(*buffer);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, EventType::OrderRejected);
}

TEST_F(GatewayTest, AcceptMarketWithZeroPrice) {
    // Market order with price=0 must not be rejected by gateway validation.
    // No sell liquidity — engine cancels the unfilled Market order.
    auto msg = make_order_msg(1, Side::Buy, OrderType::Market, 0, 5);
    auto result = gateway->process_order(msg);

    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.reject_reason, GatewayRejectReason::None);
    EXPECT_EQ(result.match_status, MatchStatus::Cancelled);
}

TEST_F(GatewayTest, RejectPoolExhausted) {
    // Create a tiny pool (capacity 1)
    MemoryPool<Order> tiny_pool(1);
    MatchingEngine tiny_engine(*book, tiny_pool);
    OrderGateway tiny_gw(tiny_engine, tiny_pool, buffer.get());

    // First order uses the only slot
    auto msg1 = make_order_msg(1, Side::Buy, OrderType::Limit,
                               100 * PRICE_SCALE, 10);
    auto r1 = tiny_gw.process_order(msg1);
    EXPECT_TRUE(r1.accepted);
    drain_events(*buffer);

    // Second order should fail — pool exhausted
    auto msg2 = make_order_msg(2, Side::Buy, OrderType::Limit,
                               100 * PRICE_SCALE, 10);
    auto r2 = tiny_gw.process_order(msg2);

    EXPECT_FALSE(r2.accepted);
    EXPECT_EQ(r2.reject_reason, GatewayRejectReason::PoolExhausted);

    auto events = drain_events(*buffer);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, EventType::OrderRejected);
}

// ===========================================================================
// Gateway -> Matching engine integration
// ===========================================================================

TEST_F(GatewayTest, LimitOrderRests) {
    auto msg = make_order_msg(1, Side::Buy, OrderType::Limit,
                              100 * PRICE_SCALE, 10);
    auto result = gateway->process_order(msg);

    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.match_status, MatchStatus::Resting);
    EXPECT_EQ(result.trade_count, 0u);
    EXPECT_EQ(result.filled_quantity, 0u);
    EXPECT_EQ(result.remaining_quantity, 10u);
    EXPECT_EQ(book->order_count(), 1u);
}

TEST_F(GatewayTest, LimitOrderMatches) {
    // Place a sell order (participant 1)
    auto sell = make_order_msg(1, Side::Sell, OrderType::Limit,
                               100 * PRICE_SCALE, 10, /*participant=*/1);
    (void)gateway->process_order(sell);
    drain_events(*buffer);

    // Place a crossing buy order (participant 2 — avoids STP)
    auto buy = make_order_msg(2, Side::Buy, OrderType::Limit,
                              100 * PRICE_SCALE, 10, /*participant=*/2);
    auto result = gateway->process_order(buy);

    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.match_status, MatchStatus::Filled);
    EXPECT_EQ(result.trade_count, 1u);
    EXPECT_EQ(result.filled_quantity, 10u);
    EXPECT_EQ(result.remaining_quantity, 0u);
    EXPECT_EQ(book->order_count(), 0u);
}

TEST_F(GatewayTest, CancelSucceeds) {
    auto msg = make_order_msg(1, Side::Buy, OrderType::Limit,
                              100 * PRICE_SCALE, 10);
    (void)gateway->process_order(msg);
    EXPECT_EQ(book->order_count(), 1u);

    bool cancelled = gateway->process_cancel(1);
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(book->order_count(), 0u);
}

TEST_F(GatewayTest, CancelNonexistentFails) {
    bool cancelled = gateway->process_cancel(999);
    EXPECT_FALSE(cancelled);
}

// ===========================================================================
// Event publishing
// ===========================================================================

TEST_F(GatewayTest, RestingPublishesOrderAccepted) {
    auto msg = make_order_msg(1, Side::Buy, OrderType::Limit,
                              100 * PRICE_SCALE, 10);
    (void)gateway->process_order(msg);

    auto events = drain_events(*buffer);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, EventType::OrderAccepted);
    EXPECT_EQ(events[0].data.order_event.order_id, 1u);
    EXPECT_EQ(events[0].data.order_event.status, OrderStatus::Accepted);
    EXPECT_EQ(events[0].data.order_event.remaining_quantity, 10u);
}

TEST_F(GatewayTest, FullFillPublishesTradeAndOrderFilled) {
    // Sell resting at 100 (participant 1)
    auto sell = make_order_msg(1, Side::Sell, OrderType::Limit,
                               100 * PRICE_SCALE, 10, /*participant=*/1);
    (void)gateway->process_order(sell);
    drain_events(*buffer);

    // Buy crosses at 100 for full fill (participant 2)
    auto buy = make_order_msg(2, Side::Buy, OrderType::Limit,
                              100 * PRICE_SCALE, 10, /*participant=*/2);
    (void)gateway->process_order(buy);

    auto events = drain_events(*buffer);
    ASSERT_EQ(events.size(), 2u);

    // First event: Trade
    EXPECT_EQ(events[0].type, EventType::Trade);
    EXPECT_EQ(events[0].data.trade.quantity, 10u);
    EXPECT_EQ(events[0].data.trade.price, 100 * PRICE_SCALE);

    // Second event: OrderFilled
    EXPECT_EQ(events[1].type, EventType::OrderFilled);
    EXPECT_EQ(events[1].data.order_event.order_id, 2u);
    EXPECT_EQ(events[1].data.order_event.filled_quantity, 10u);
    EXPECT_EQ(events[1].data.order_event.remaining_quantity, 0u);
}

TEST_F(GatewayTest, PartialFillPublishesTradeAndOrderPartialFill) {
    // Sell resting at 100, qty 5 (participant 1)
    auto sell = make_order_msg(1, Side::Sell, OrderType::Limit,
                               100 * PRICE_SCALE, 5, /*participant=*/1);
    (void)gateway->process_order(sell);
    drain_events(*buffer);

    // Buy at 100, qty 10 — partial fill (5 filled, 5 rests) (participant 2)
    auto buy = make_order_msg(2, Side::Buy, OrderType::Limit,
                              100 * PRICE_SCALE, 10, /*participant=*/2);
    (void)gateway->process_order(buy);

    auto events = drain_events(*buffer);
    ASSERT_EQ(events.size(), 2u);

    EXPECT_EQ(events[0].type, EventType::Trade);
    EXPECT_EQ(events[0].data.trade.quantity, 5u);

    EXPECT_EQ(events[1].type, EventType::OrderPartialFill);
    EXPECT_EQ(events[1].data.order_event.order_id, 2u);
    EXPECT_EQ(events[1].data.order_event.filled_quantity, 5u);
    EXPECT_EQ(events[1].data.order_event.remaining_quantity, 5u);
}

TEST_F(GatewayTest, MultiLevelCrossPublishesMultipleTrades) {
    // Sell at 100, qty 5 (participant 1)
    auto sell1 = make_order_msg(1, Side::Sell, OrderType::Limit,
                                100 * PRICE_SCALE, 5, /*participant=*/1);
    (void)gateway->process_order(sell1);

    // Sell at 101, qty 5 (participant 1)
    auto sell2 = make_order_msg(2, Side::Sell, OrderType::Limit,
                                101 * PRICE_SCALE, 5, /*participant=*/1);
    (void)gateway->process_order(sell2);
    drain_events(*buffer);

    // Buy at 101, qty 10 — sweeps both levels (participant 2)
    auto buy = make_order_msg(3, Side::Buy, OrderType::Limit,
                              101 * PRICE_SCALE, 10, /*participant=*/2);
    (void)gateway->process_order(buy);

    auto events = drain_events(*buffer);
    ASSERT_EQ(events.size(), 3u);

    // Two trades (100 first, then 101 — price-time priority)
    EXPECT_EQ(events[0].type, EventType::Trade);
    EXPECT_EQ(events[0].data.trade.price, 100 * PRICE_SCALE);
    EXPECT_EQ(events[0].data.trade.quantity, 5u);

    EXPECT_EQ(events[1].type, EventType::Trade);
    EXPECT_EQ(events[1].data.trade.price, 101 * PRICE_SCALE);
    EXPECT_EQ(events[1].data.trade.quantity, 5u);

    // Terminal status: fully filled
    EXPECT_EQ(events[2].type, EventType::OrderFilled);
    EXPECT_EQ(events[2].data.order_event.order_id, 3u);
}

TEST_F(GatewayTest, CancelPublishesOrderCancelled) {
    auto msg = make_order_msg(1, Side::Buy, OrderType::Limit,
                              100 * PRICE_SCALE, 10);
    (void)gateway->process_order(msg);
    drain_events(*buffer);

    (void)gateway->process_cancel(1);

    auto events = drain_events(*buffer);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, EventType::OrderCancelled);
    EXPECT_EQ(events[0].data.order_event.order_id, 1u);
    EXPECT_EQ(events[0].data.order_event.status, OrderStatus::Cancelled);
}

TEST_F(GatewayTest, FOKRejectionPublishesOrderRejected) {
    // FOK buy for qty 100 with no sell liquidity — engine rejects
    auto msg = make_order_msg(1, Side::Buy, OrderType::FOK,
                              100 * PRICE_SCALE, 100);
    msg.order.time_in_force = TimeInForce::FOK;
    auto result = gateway->process_order(msg);

    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.match_status, MatchStatus::Rejected);

    auto events = drain_events(*buffer);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, EventType::OrderRejected);
    EXPECT_EQ(events[0].data.order_event.order_id, 1u);
}

TEST_F(GatewayTest, SequenceNumbersMonotonicallyIncrease) {
    // Submit several orders
    for (OrderId i = 1; i <= 5; ++i) {
        auto msg = make_order_msg(i, Side::Buy, OrderType::Limit,
                                  (100 - i) * PRICE_SCALE, 10);
        (void)gateway->process_order(msg);
    }

    auto events = drain_events(*buffer);
    ASSERT_GE(events.size(), 5u);  // At least 5 OrderAccepted events

    for (size_t i = 1; i < events.size(); ++i) {
        EXPECT_GT(events[i].sequence_num, events[i - 1].sequence_num)
            << "Sequence not monotonic at index " << i;
    }
}

TEST_F(GatewayTest, TradesPublishedBeforeStatusEvents) {
    // Sell resting (participant 1)
    auto sell = make_order_msg(1, Side::Sell, OrderType::Limit,
                               100 * PRICE_SCALE, 10, /*participant=*/1);
    (void)gateway->process_order(sell);
    drain_events(*buffer);

    // Buy crosses — generates 1 Trade + 1 OrderFilled (participant 2)
    auto buy = make_order_msg(2, Side::Buy, OrderType::Limit,
                              100 * PRICE_SCALE, 10, /*participant=*/2);
    (void)gateway->process_order(buy);

    auto events = drain_events(*buffer);
    ASSERT_EQ(events.size(), 2u);

    // Trade must precede the status event
    EXPECT_EQ(events[0].type, EventType::Trade);
    EXPECT_NE(events[1].type, EventType::Trade);
}

TEST_F(GatewayTest, SelfTradePreventedPublishesOrderCancelled) {
    // Same participant (42) on both sides
    auto sell = make_order_msg(1, Side::Sell, OrderType::Limit,
                               100 * PRICE_SCALE, 10, /*participant=*/42);
    (void)gateway->process_order(sell);
    drain_events(*buffer);

    auto buy = make_order_msg(2, Side::Buy, OrderType::Limit,
                              100 * PRICE_SCALE, 10, /*participant=*/42);
    auto result = gateway->process_order(buy);

    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.match_status, MatchStatus::SelfTradePrevented);

    auto events = drain_events(*buffer);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, EventType::OrderCancelled);
    EXPECT_EQ(events[0].data.order_event.order_id, 2u);
}

TEST_F(GatewayTest, IOCPartialFillPublishesTradeAndOrderCancelled) {
    // Sell resting at 100, qty 5 (participant 1)
    auto sell = make_order_msg(1, Side::Sell, OrderType::Limit,
                               100 * PRICE_SCALE, 5, /*participant=*/1);
    (void)gateway->process_order(sell);
    drain_events(*buffer);

    // IOC buy at 100, qty 10 — fills 5, remainder cancelled (participant 2)
    auto buy = make_order_msg(2, Side::Buy, OrderType::IOC,
                              100 * PRICE_SCALE, 10, /*participant=*/2);
    buy.order.time_in_force = TimeInForce::IOC;
    auto result = gateway->process_order(buy);

    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.match_status, MatchStatus::Cancelled);
    EXPECT_EQ(result.filled_quantity, 5u);

    auto events = drain_events(*buffer);
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].type, EventType::Trade);
    EXPECT_EQ(events[0].data.trade.quantity, 5u);
    EXPECT_EQ(events[1].type, EventType::OrderCancelled);
    EXPECT_EQ(events[1].data.order_event.order_id, 2u);
}

// ===========================================================================
// Modify tests
// ===========================================================================

TEST_F(GatewayTest, ModifySuccess) {
    // Place a resting buy
    auto msg = make_order_msg(1, Side::Buy, OrderType::Limit,
                              100 * PRICE_SCALE, 10);
    (void)gateway->process_order(msg);
    drain_events(*buffer);
    EXPECT_EQ(book->order_count(), 1u);

    // Modify its price
    OrderMessage modify_msg{};
    modify_msg.type = MessageType::Modify;
    modify_msg.order.order_id = 1;
    modify_msg.order.price = 99 * PRICE_SCALE;
    modify_msg.order.quantity = 10;
    modify_msg.order.timestamp = 2000;

    auto result = gateway->process_modify(modify_msg);
    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.match_status, MatchStatus::Modified);
    EXPECT_EQ(result.trade_count, 0u);
    EXPECT_EQ(book->order_count(), 1u);
    EXPECT_EQ(book->best_bid()->price, 99 * PRICE_SCALE);
}

TEST_F(GatewayTest, ModifyNotFound) {
    OrderMessage modify_msg{};
    modify_msg.type = MessageType::Modify;
    modify_msg.order.order_id = 999;
    modify_msg.order.price = 100 * PRICE_SCALE;
    modify_msg.order.quantity = 10;
    modify_msg.order.timestamp = 2000;

    auto result = gateway->process_modify(modify_msg);
    EXPECT_FALSE(result.accepted);
    EXPECT_EQ(result.reject_reason, GatewayRejectReason::OrderNotFound);
}

TEST_F(GatewayTest, ModifyPublishesEvents) {
    // Place and drain
    auto msg = make_order_msg(1, Side::Buy, OrderType::Limit,
                              100 * PRICE_SCALE, 10);
    (void)gateway->process_order(msg);
    drain_events(*buffer);

    // Modify
    OrderMessage modify_msg{};
    modify_msg.type = MessageType::Modify;
    modify_msg.order.order_id = 1;
    modify_msg.order.price = 99 * PRICE_SCALE;
    modify_msg.order.quantity = 10;
    modify_msg.order.timestamp = 2000;
    (void)gateway->process_modify(modify_msg);

    auto events = drain_events(*buffer);
    ASSERT_GE(events.size(), 1u);
    EXPECT_EQ(events.back().type, EventType::OrderModified);
    EXPECT_EQ(events.back().data.order_event.order_id, 1u);
}

TEST_F(GatewayTest, ModifyCrossing) {
    // Place a sell at 100 (participant 1)
    auto sell = make_order_msg(1, Side::Sell, OrderType::Limit,
                               100 * PRICE_SCALE, 10, /*participant=*/1);
    (void)gateway->process_order(sell);

    // Place a buy at 99 (participant 2)
    auto buy = make_order_msg(2, Side::Buy, OrderType::Limit,
                              99 * PRICE_SCALE, 10, /*participant=*/2);
    (void)gateway->process_order(buy);
    drain_events(*buffer);

    // Modify buy price to cross the sell
    OrderMessage modify_msg{};
    modify_msg.type = MessageType::Modify;
    modify_msg.order.order_id = 2;
    modify_msg.order.price = 100 * PRICE_SCALE;
    modify_msg.order.quantity = 10;
    modify_msg.order.timestamp = 3000;

    auto result = gateway->process_modify(modify_msg);
    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.match_status, MatchStatus::Filled);
    EXPECT_EQ(result.trade_count, 1u);
    EXPECT_EQ(result.filled_quantity, 10u);
    EXPECT_TRUE(book->empty());

    auto events = drain_events(*buffer);
    // Should have Trade + OrderFilled
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].type, EventType::Trade);
    EXPECT_EQ(events[1].type, EventType::OrderFilled);
}

TEST_F(GatewayTest, ModifyRejectZeroQuantity) {
    auto msg = make_order_msg(1, Side::Buy, OrderType::Limit,
                              100 * PRICE_SCALE, 10);
    (void)gateway->process_order(msg);
    drain_events(*buffer);

    OrderMessage modify_msg{};
    modify_msg.type = MessageType::Modify;
    modify_msg.order.order_id = 1;
    modify_msg.order.price = 100 * PRICE_SCALE;
    modify_msg.order.quantity = 0;
    modify_msg.order.timestamp = 2000;

    auto result = gateway->process_modify(modify_msg);
    EXPECT_FALSE(result.accepted);
    EXPECT_EQ(result.reject_reason, GatewayRejectReason::InvalidQuantity);
}

TEST_F(GatewayTest, ModifyRejectZeroPrice) {
    auto msg = make_order_msg(1, Side::Buy, OrderType::Limit,
                              100 * PRICE_SCALE, 10);
    (void)gateway->process_order(msg);
    drain_events(*buffer);

    OrderMessage modify_msg{};
    modify_msg.type = MessageType::Modify;
    modify_msg.order.order_id = 1;
    modify_msg.order.price = 0;
    modify_msg.order.quantity = 10;
    modify_msg.order.timestamp = 2000;

    auto result = gateway->process_modify(modify_msg);
    EXPECT_FALSE(result.accepted);
    EXPECT_EQ(result.reject_reason, GatewayRejectReason::InvalidPrice);
}

// ===========================================================================
// MarketDataPublisher tests
// ===========================================================================

TEST_F(GatewayTest, PollReturnsZeroOnEmpty) {
    MarketDataPublisher publisher(*buffer);
    EXPECT_EQ(publisher.poll(), 0u);
}

TEST_F(GatewayTest, PollDrainsAll) {
    MarketDataPublisher publisher(*buffer);
    std::vector<EventMessage> received;
    publisher.register_callback([&](const EventMessage& e) {
        received.push_back(e);
    });

    // Submit 3 resting orders — 3 OrderAccepted events in buffer
    for (OrderId i = 1; i <= 3; ++i) {
        auto msg = make_order_msg(i, Side::Buy, OrderType::Limit,
                                  (100 - i) * PRICE_SCALE, 10);
        (void)gateway->process_order(msg);
    }

    size_t count = publisher.poll();
    EXPECT_EQ(count, 3u);
    EXPECT_EQ(received.size(), 3u);
    EXPECT_EQ(publisher.events_processed(), 3u);

    // Second poll returns 0
    EXPECT_EQ(publisher.poll(), 0u);
}

TEST_F(GatewayTest, CallbacksInvokedPerEvent) {
    MarketDataPublisher publisher(*buffer);
    size_t cb_count = 0;
    publisher.register_callback([&](const EventMessage&) {
        ++cb_count;
    });

    auto msg = make_order_msg(1, Side::Buy, OrderType::Limit,
                              100 * PRICE_SCALE, 10);
    (void)gateway->process_order(msg);

    (void)publisher.poll();
    EXPECT_EQ(cb_count, 1u);
}

TEST_F(GatewayTest, MultipleCallbacksAllFire) {
    MarketDataPublisher publisher(*buffer);
    size_t cb1_count = 0;
    size_t cb2_count = 0;
    publisher.register_callback([&](const EventMessage&) { ++cb1_count; });
    publisher.register_callback([&](const EventMessage&) { ++cb2_count; });

    auto msg = make_order_msg(1, Side::Buy, OrderType::Limit,
                              100 * PRICE_SCALE, 10);
    (void)gateway->process_order(msg);

    (void)publisher.poll();
    EXPECT_EQ(cb1_count, 1u);
    EXPECT_EQ(cb2_count, 1u);
}

TEST_F(GatewayTest, LastSequenceNumTracked) {
    MarketDataPublisher publisher(*buffer);
    publisher.register_callback([](const EventMessage&) {});

    EXPECT_EQ(publisher.last_sequence_num(), 0u);

    // Submit an order — produces 1 event
    auto msg = make_order_msg(1, Side::Buy, OrderType::Limit,
                              100 * PRICE_SCALE, 10);
    (void)gateway->process_order(msg);
    (void)publisher.poll();

    EXPECT_EQ(publisher.last_sequence_num(), 1u);

    // Submit another
    auto msg2 = make_order_msg(2, Side::Buy, OrderType::Limit,
                               99 * PRICE_SCALE, 10);
    (void)gateway->process_order(msg2);
    (void)publisher.poll();

    EXPECT_EQ(publisher.last_sequence_num(), 2u);
}

// ===========================================================================
// Null buffer
// ===========================================================================

TEST_F(GatewayTest, NullBufferNoCrash) {
    // Gateway with no event buffer
    OrderGateway null_gw(*engine, *pool, nullptr);

    auto msg = make_order_msg(1, Side::Buy, OrderType::Limit,
                              100 * PRICE_SCALE, 10);
    auto result = null_gw.process_order(msg);
    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.match_status, MatchStatus::Resting);

    // Cancel also works without crash
    bool cancelled = null_gw.process_cancel(1);
    EXPECT_TRUE(cancelled);

    // Rejection also works
    auto bad = make_order_msg(2, Side::Buy, OrderType::Limit, 0, 10);
    auto r2 = null_gw.process_order(bad);
    EXPECT_FALSE(r2.accepted);
}

// ===========================================================================
// Multi-threaded end-to-end
// ===========================================================================

TEST_F(GatewayTest, MultiThreadedE2E) {
    MarketDataPublisher publisher(*buffer);
    std::vector<EventMessage> received;
    publisher.register_callback([&](const EventMessage& e) {
        received.push_back(e);
    });

    std::thread pub_thread([&publisher]() {
        publisher.run();
    });

    // Submit 200 resting buy orders from the main thread
    constexpr int ORDER_COUNT = 200;
    for (int i = 0; i < ORDER_COUNT; ++i) {
        auto msg = make_order_msg(
            static_cast<OrderId>(i + 1), Side::Buy, OrderType::Limit,
            static_cast<Price>((100 - (i % 50)) * PRICE_SCALE),
            10);
        (void)gateway->process_order(msg);
    }

    // Give publisher time to drain
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    publisher.stop();
    pub_thread.join();

    // Every order rests — 200 OrderAccepted events
    EXPECT_EQ(received.size(), static_cast<size_t>(ORDER_COUNT));
    EXPECT_EQ(publisher.events_processed(),
              static_cast<uint64_t>(ORDER_COUNT));

    // Verify monotonic sequence numbers
    for (size_t i = 1; i < received.size(); ++i) {
        EXPECT_GT(received[i].sequence_num, received[i - 1].sequence_num);
    }
}

// ===========================================================================
// Statistics
// ===========================================================================

TEST_F(GatewayTest, OrdersProcessedCounter) {
    EXPECT_EQ(gateway->orders_processed(), 0u);

    auto msg1 = make_order_msg(1, Side::Buy, OrderType::Limit,
                               100 * PRICE_SCALE, 10);
    (void)gateway->process_order(msg1);

    auto msg2 = make_order_msg(2, Side::Sell, OrderType::Limit,
                               101 * PRICE_SCALE, 10);
    (void)gateway->process_order(msg2);

    EXPECT_EQ(gateway->orders_processed(), 2u);
}

TEST_F(GatewayTest, OrdersRejectedCounter) {
    EXPECT_EQ(gateway->orders_rejected(), 0u);

    // Invalid quantity
    auto bad1 = make_order_msg(1, Side::Buy, OrderType::Limit,
                               100 * PRICE_SCALE, 0);
    (void)gateway->process_order(bad1);

    // Invalid price
    auto bad2 = make_order_msg(2, Side::Buy, OrderType::Limit,
                               -1 * PRICE_SCALE, 10);
    (void)gateway->process_order(bad2);

    EXPECT_EQ(gateway->orders_rejected(), 2u);
    EXPECT_EQ(gateway->orders_processed(), 0u);
}
