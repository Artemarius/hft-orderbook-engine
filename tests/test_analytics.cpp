/// @file test_analytics.cpp
/// @brief Unit and integration tests for the analytics module (Phase 7).

#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "analytics/analytics_config.h"
#include "analytics/analytics_engine.h"
#include "analytics/depth_profile.h"
#include "analytics/microprice_calculator.h"
#include "analytics/order_flow_imbalance.h"
#include "analytics/price_impact.h"
#include "analytics/realized_volatility.h"
#include "analytics/spread_analytics.h"
#include "core/types.h"
#include "orderbook/memory_pool.h"
#include "orderbook/order_book.h"
#include "transport/message.h"

using namespace hft;

// ============================================================================
// Test helpers
// ============================================================================

namespace {

constexpr Price MIN_PRICE = 100 * PRICE_SCALE;   // $100
constexpr Price MAX_PRICE = 200 * PRICE_SCALE;    // $200
constexpr Price TICK = PRICE_SCALE / 100;          // $0.01

/// Build a dummy EventMessage of a given type.
EventMessage make_event(EventType type, uint64_t seq = 0) {
    EventMessage msg{};
    msg.type = type;
    msg.sequence_num = seq;
    return msg;
}

/// Build a Trade EventMessage.
EventMessage make_trade(Price price, Quantity qty, uint64_t seq = 0,
                        Timestamp ts = 0) {
    EventMessage msg{};
    msg.type = EventType::Trade;
    msg.sequence_num = seq;
    msg.data.trade.price = price;
    msg.data.trade.quantity = qty;
    msg.data.trade.timestamp = ts;
    return msg;
}

/// Build an OrderAccepted EventMessage.
EventMessage make_accepted(OrderId id, Price price, uint64_t seq = 0,
                           Timestamp ts = 0) {
    EventMessage msg{};
    msg.type = EventType::OrderAccepted;
    msg.sequence_num = seq;
    msg.data.order_event.order_id = id;
    msg.data.order_event.price = price;
    msg.data.order_event.timestamp = ts;
    msg.data.order_event.status = OrderStatus::Accepted;
    return msg;
}

/// Helper: place a buy order on the book.
Order* place_buy(OrderBook& book, MemoryPool<Order>& pool,
                 OrderId id, Price price, Quantity qty) {
    Order* o = pool.allocate();
    o->order_id = id;
    o->side = Side::Buy;
    o->price = price;
    o->quantity = qty;
    o->visible_quantity = qty;
    o->filled_quantity = 0;
    o->type = OrderType::Limit;
    o->time_in_force = TimeInForce::GTC;
    o->status = OrderStatus::New;
    o->participant_id = 1;
    o->timestamp = 0;
    o->next = nullptr;
    o->prev = nullptr;
    book.add_order(o);
    return o;
}

/// Helper: place a sell order on the book.
Order* place_sell(OrderBook& book, MemoryPool<Order>& pool,
                  OrderId id, Price price, Quantity qty) {
    Order* o = pool.allocate();
    o->order_id = id;
    o->side = Side::Sell;
    o->price = price;
    o->quantity = qty;
    o->visible_quantity = qty;
    o->filled_quantity = 0;
    o->type = OrderType::Limit;
    o->time_in_force = TimeInForce::GTC;
    o->status = OrderStatus::New;
    o->participant_id = 1;
    o->timestamp = 0;
    o->next = nullptr;
    o->prev = nullptr;
    book.add_order(o);
    return o;
}

}  // namespace

// ============================================================================
// OrderBook depth API tests
// ============================================================================

class OrderBookDepthTest : public ::testing::Test {
protected:
    OrderBook book{MIN_PRICE, MAX_PRICE, TICK, 1000};
    MemoryPool<Order> pool{1000};
};

TEST_F(OrderBookDepthTest, EmptyBook) {
    DepthEntry out[5];
    EXPECT_EQ(book.get_bid_depth(out, 5), 0u);
    EXPECT_EQ(book.get_ask_depth(out, 5), 0u);
}

TEST_F(OrderBookDepthTest, SingleLevel) {
    place_buy(book, pool, 1, 150 * PRICE_SCALE, 10);
    place_sell(book, pool, 2, 151 * PRICE_SCALE, 20);

    DepthEntry bid_out[5], ask_out[5];
    size_t bid_n = book.get_bid_depth(bid_out, 5);
    size_t ask_n = book.get_ask_depth(ask_out, 5);

    ASSERT_EQ(bid_n, 1u);
    EXPECT_EQ(bid_out[0].price, 150 * PRICE_SCALE);
    EXPECT_EQ(bid_out[0].quantity, 10u);
    EXPECT_EQ(bid_out[0].order_count, 1u);

    ASSERT_EQ(ask_n, 1u);
    EXPECT_EQ(ask_out[0].price, 151 * PRICE_SCALE);
    EXPECT_EQ(ask_out[0].quantity, 20u);
    EXPECT_EQ(ask_out[0].order_count, 1u);
}

TEST_F(OrderBookDepthTest, MultipleLevels) {
    // Build 3 bid levels, 3 ask levels
    place_buy(book, pool, 1, 150 * PRICE_SCALE, 10);
    place_buy(book, pool, 2, 149 * PRICE_SCALE, 20);
    place_buy(book, pool, 3, 148 * PRICE_SCALE, 30);
    place_sell(book, pool, 4, 151 * PRICE_SCALE, 5);
    place_sell(book, pool, 5, 152 * PRICE_SCALE, 15);
    place_sell(book, pool, 6, 153 * PRICE_SCALE, 25);

    DepthEntry bid_out[10], ask_out[10];
    size_t bid_n = book.get_bid_depth(bid_out, 10);
    size_t ask_n = book.get_ask_depth(ask_out, 10);

    ASSERT_EQ(bid_n, 3u);
    // Best bid first (highest price)
    EXPECT_EQ(bid_out[0].price, 150 * PRICE_SCALE);
    EXPECT_EQ(bid_out[1].price, 149 * PRICE_SCALE);
    EXPECT_EQ(bid_out[2].price, 148 * PRICE_SCALE);

    ASSERT_EQ(ask_n, 3u);
    // Best ask first (lowest price)
    EXPECT_EQ(ask_out[0].price, 151 * PRICE_SCALE);
    EXPECT_EQ(ask_out[1].price, 152 * PRICE_SCALE);
    EXPECT_EQ(ask_out[2].price, 153 * PRICE_SCALE);
}

TEST_F(OrderBookDepthTest, MaxLevelsLimit) {
    place_buy(book, pool, 1, 150 * PRICE_SCALE, 10);
    place_buy(book, pool, 2, 149 * PRICE_SCALE, 20);
    place_buy(book, pool, 3, 148 * PRICE_SCALE, 30);

    DepthEntry bid_out[2];
    size_t n = book.get_bid_depth(bid_out, 2);
    EXPECT_EQ(n, 2u);
    EXPECT_EQ(bid_out[0].price, 150 * PRICE_SCALE);
    EXPECT_EQ(bid_out[1].price, 149 * PRICE_SCALE);
}

TEST_F(OrderBookDepthTest, MultipleOrdersSameLevel) {
    place_buy(book, pool, 1, 150 * PRICE_SCALE, 10);
    place_buy(book, pool, 2, 150 * PRICE_SCALE, 20);
    place_buy(book, pool, 3, 150 * PRICE_SCALE, 30);

    DepthEntry bid_out[5];
    size_t n = book.get_bid_depth(bid_out, 5);
    ASSERT_EQ(n, 1u);
    EXPECT_EQ(bid_out[0].price, 150 * PRICE_SCALE);
    EXPECT_EQ(bid_out[0].quantity, 60u);
    EXPECT_EQ(bid_out[0].order_count, 3u);
}

// ============================================================================
// SpreadAnalytics tests
// ============================================================================

class SpreadAnalyticsTest : public ::testing::Test {
protected:
    OrderBook book{MIN_PRICE, MAX_PRICE, TICK, 1000};
    MemoryPool<Order> pool{1000};
    SpreadAnalytics spread;
};

TEST_F(SpreadAnalyticsTest, EmptyBook) {
    auto event = make_accepted(1, 150 * PRICE_SCALE);
    spread.on_event(event, book);
    EXPECT_EQ(spread.current_spread(), -1);
    EXPECT_DOUBLE_EQ(spread.current_spread_bps(), 0.0);
}

TEST_F(SpreadAnalyticsTest, NormalSpread) {
    Price bid = 150 * PRICE_SCALE;
    Price ask = 151 * PRICE_SCALE;
    place_buy(book, pool, 1, bid, 10);
    place_sell(book, pool, 2, ask, 10);

    auto event = make_accepted(3, bid);
    spread.on_event(event, book);

    EXPECT_EQ(spread.current_spread(), ask - bid);
    EXPECT_GT(spread.current_spread_bps(), 0.0);
    EXPECT_DOUBLE_EQ(spread.avg_spread_bps(), spread.current_spread_bps());
}

TEST_F(SpreadAnalyticsTest, EffectiveSpread) {
    Price bid = 150 * PRICE_SCALE;
    Price ask = 151 * PRICE_SCALE;
    place_buy(book, pool, 1, bid, 10);
    place_sell(book, pool, 2, ask, 10);

    // First event to establish mid price
    auto event1 = make_accepted(3, bid);
    spread.on_event(event1, book);

    Price mid = book.mid_price();
    EXPECT_GT(mid, 0);

    // Trade at the ask (aggressive buy)
    auto trade = make_trade(ask, 5, 1);
    spread.on_event(trade, book);

    // Effective spread = 2 * |trade_price - prev_mid|
    Price expected = 2 * std::abs(ask - mid);
    EXPECT_EQ(spread.last_effective_spread(), expected);
    EXPECT_GT(spread.avg_effective_spread_bps(), 0.0);
}

TEST_F(SpreadAnalyticsTest, JsonOutput) {
    Price bid = 150 * PRICE_SCALE;
    Price ask = 151 * PRICE_SCALE;
    place_buy(book, pool, 1, bid, 10);
    place_sell(book, pool, 2, ask, 10);

    auto event = make_accepted(3, bid);
    spread.on_event(event, book);

    auto j = spread.to_json();
    EXPECT_TRUE(j.contains("current_spread_bps"));
    EXPECT_TRUE(j.contains("avg_spread_bps"));
    EXPECT_TRUE(j.contains("avg_effective_spread_bps"));
    EXPECT_TRUE(j.contains("spread_samples"));
}

// ============================================================================
// MicropriceCalculator tests
// ============================================================================

class MicropriceTest : public ::testing::Test {
protected:
    OrderBook book{MIN_PRICE, MAX_PRICE, TICK, 1000};
    MemoryPool<Order> pool{1000};
    MicropriceCalculator mp;
};

TEST_F(MicropriceTest, EmptyBook) {
    auto event = make_accepted(1, 150 * PRICE_SCALE);
    mp.on_event(event, book);
    EXPECT_FALSE(mp.is_valid());
}

TEST_F(MicropriceTest, SymmetricDepth) {
    Price bid = 150 * PRICE_SCALE;
    Price ask = 151 * PRICE_SCALE;
    place_buy(book, pool, 1, bid, 10);
    place_sell(book, pool, 2, ask, 10);

    auto event = make_accepted(3, bid);
    mp.on_event(event, book);

    EXPECT_TRUE(mp.is_valid());
    // Symmetric: microprice = midpoint
    double expected_mid = (static_cast<double>(bid) + static_cast<double>(ask)) / 2.0;
    EXPECT_NEAR(mp.current_microprice(), expected_mid, 1.0);
}

TEST_F(MicropriceTest, AsymmetricDepth) {
    Price bid = 150 * PRICE_SCALE;
    Price ask = 151 * PRICE_SCALE;
    // More bid qty => microprice skews toward ask
    place_buy(book, pool, 1, bid, 30);
    place_sell(book, pool, 2, ask, 10);

    auto event = make_accepted(3, bid);
    mp.on_event(event, book);

    EXPECT_TRUE(mp.is_valid());
    // bid_qty * ask_px + ask_qty * bid_px) / (bid_qty + ask_qty)
    double expected = (30.0 * static_cast<double>(ask) +
                       10.0 * static_cast<double>(bid)) / 40.0;
    EXPECT_NEAR(mp.current_microprice(), expected, 1.0);

    // Microprice should be closer to ask than bid (more bid depth)
    double mid = (static_cast<double>(bid) + static_cast<double>(ask)) / 2.0;
    EXPECT_GT(mp.current_microprice(), mid);
}

TEST_F(MicropriceTest, JsonOutput) {
    Price bid = 150 * PRICE_SCALE;
    Price ask = 151 * PRICE_SCALE;
    place_buy(book, pool, 1, bid, 10);
    place_sell(book, pool, 2, ask, 10);

    auto event = make_accepted(3, bid);
    mp.on_event(event, book);

    auto j = mp.to_json();
    EXPECT_TRUE(j.contains("microprice"));
    EXPECT_TRUE(j.contains("valid"));
    EXPECT_TRUE(j["valid"].get<bool>());
}

// ============================================================================
// OrderFlowImbalance tests
// ============================================================================

class OrderFlowImbalanceTest : public ::testing::Test {
protected:
    OrderBook book{MIN_PRICE, MAX_PRICE, TICK, 1000};
    MemoryPool<Order> pool{1000};
};

TEST_F(OrderFlowImbalanceTest, PureBuyFlow) {
    OrderFlowImbalance ofi(10);
    Price price = 150 * PRICE_SCALE;

    for (int i = 0; i < 5; ++i) {
        auto trade = make_trade(price, 10, i);
        ofi.on_event(trade, book, Side::Buy);
    }

    EXPECT_EQ(ofi.sample_count(), 5u);
    EXPECT_DOUBLE_EQ(ofi.current_imbalance(), 1.0);
}

TEST_F(OrderFlowImbalanceTest, PureSellFlow) {
    OrderFlowImbalance ofi(10);
    Price price = 150 * PRICE_SCALE;

    for (int i = 0; i < 5; ++i) {
        auto trade = make_trade(price, 10, i);
        ofi.on_event(trade, book, Side::Sell);
    }

    EXPECT_DOUBLE_EQ(ofi.current_imbalance(), -1.0);
}

TEST_F(OrderFlowImbalanceTest, BalancedFlow) {
    OrderFlowImbalance ofi(10);
    Price price = 150 * PRICE_SCALE;

    auto buy_trade = make_trade(price, 10, 0);
    ofi.on_event(buy_trade, book, Side::Buy);

    auto sell_trade = make_trade(price, 10, 1);
    ofi.on_event(sell_trade, book, Side::Sell);

    EXPECT_DOUBLE_EQ(ofi.current_imbalance(), 0.0);
}

TEST_F(OrderFlowImbalanceTest, WindowEviction) {
    OrderFlowImbalance ofi(3);
    Price price = 150 * PRICE_SCALE;

    // Fill window with buys
    for (int i = 0; i < 3; ++i) {
        auto trade = make_trade(price, 10, i);
        ofi.on_event(trade, book, Side::Buy);
    }
    EXPECT_DOUBLE_EQ(ofi.current_imbalance(), 1.0);
    EXPECT_EQ(ofi.sample_count(), 3u);

    // Add sells to replace buys in the window
    for (int i = 0; i < 3; ++i) {
        auto trade = make_trade(price, 10, 3 + i);
        ofi.on_event(trade, book, Side::Sell);
    }
    EXPECT_DOUBLE_EQ(ofi.current_imbalance(), -1.0);
    EXPECT_EQ(ofi.sample_count(), 3u);
}

TEST_F(OrderFlowImbalanceTest, NonTradeEventsIgnored) {
    OrderFlowImbalance ofi(10);
    auto event = make_accepted(1, 150 * PRICE_SCALE);
    ofi.on_event(event, book, Side::Buy);
    EXPECT_EQ(ofi.sample_count(), 0u);
}

TEST_F(OrderFlowImbalanceTest, JsonOutput) {
    OrderFlowImbalance ofi(10);
    auto trade = make_trade(150 * PRICE_SCALE, 10, 0);
    ofi.on_event(trade, book, Side::Buy);

    auto j = ofi.to_json();
    EXPECT_TRUE(j.contains("current_imbalance"));
    EXPECT_TRUE(j.contains("sample_count"));
    EXPECT_TRUE(j.contains("window_size"));
}

// ============================================================================
// RealizedVolatility tests
// ============================================================================

class RealizedVolatilityTest : public ::testing::Test {
protected:
    OrderBook book{MIN_PRICE, MAX_PRICE, TICK, 1000};
    MemoryPool<Order> pool{1000};
};

TEST_F(RealizedVolatilityTest, NoTrades) {
    RealizedVolatility rv(10);
    EXPECT_DOUBLE_EQ(rv.tick_volatility(), 0.0);
    EXPECT_DOUBLE_EQ(rv.time_bar_volatility(), 0.0);
}

TEST_F(RealizedVolatilityTest, ConstantPrice) {
    RealizedVolatility rv(10);
    Price price = 150 * PRICE_SCALE;

    for (int i = 0; i < 5; ++i) {
        auto trade = make_trade(price, 10, i, 1000 + i);
        rv.on_event(trade, book);
    }

    // Zero volatility with constant price
    EXPECT_DOUBLE_EQ(rv.tick_volatility(), 0.0);
}

TEST_F(RealizedVolatilityTest, OscillatingPrice) {
    RealizedVolatility rv(10);
    Price p1 = 150 * PRICE_SCALE;
    Price p2 = 151 * PRICE_SCALE;

    for (int i = 0; i < 6; ++i) {
        Price price = (i % 2 == 0) ? p1 : p2;
        auto trade = make_trade(price, 10, i, 1000 + i);
        rv.on_event(trade, book);
    }

    // Oscillating prices => positive volatility
    EXPECT_GT(rv.tick_volatility(), 0.0);
}

TEST_F(RealizedVolatilityTest, WindowEviction) {
    RealizedVolatility rv(3);
    Price base = 150 * PRICE_SCALE;

    // 4 trades => 3 returns, window holds last 3
    for (int i = 0; i < 4; ++i) {
        Price price = base + static_cast<Price>(i) * TICK;
        auto trade = make_trade(price, 10, i, 1000 + i);
        rv.on_event(trade, book);
    }

    EXPECT_GT(rv.tick_volatility(), 0.0);
}

TEST_F(RealizedVolatilityTest, TimeBarVolatility) {
    // Set up book with bid/ask for mid price
    place_buy(book, pool, 100, 150 * PRICE_SCALE, 10);
    place_sell(book, pool, 200, 151 * PRICE_SCALE, 10);

    uint64_t bar_ns = 1'000'000'000;  // 1 second
    RealizedVolatility rv(50, bar_ns);

    // Trade at t=0 (initializes bar)
    auto t1 = make_trade(150 * PRICE_SCALE, 10, 0, 0);
    rv.on_event(t1, book);

    // Trade at t=1.5s (crosses bar boundary)
    auto t2 = make_trade(151 * PRICE_SCALE, 10, 1, bar_ns + bar_ns / 2);
    rv.on_event(t2, book);

    // Should have at least one bar return
    // (Depends on mid price being available)
    // The first bar crossing captures a return
    EXPECT_GE(rv.time_bar_volatility(), 0.0);
}

TEST_F(RealizedVolatilityTest, JsonOutput) {
    RealizedVolatility rv(10);
    auto j = rv.to_json();
    EXPECT_TRUE(j.contains("tick_volatility"));
    EXPECT_TRUE(j.contains("tick_return_count"));
    EXPECT_TRUE(j.contains("time_bar_volatility"));
    EXPECT_TRUE(j.contains("time_bar_count"));
}

// ============================================================================
// PriceImpact tests
// ============================================================================

class PriceImpactTest : public ::testing::Test {
protected:
    OrderBook book{MIN_PRICE, MAX_PRICE, TICK, 1000};
    MemoryPool<Order> pool{1000};
};

TEST_F(PriceImpactTest, InsufficientData) {
    PriceImpact pi(200);
    EXPECT_TRUE(std::isnan(pi.kyle_lambda()));
    EXPECT_DOUBLE_EQ(pi.avg_temporary_impact_bps(), 0.0);
    EXPECT_DOUBLE_EQ(pi.avg_permanent_impact_bps(), 0.0);
}

TEST_F(PriceImpactTest, KnownRegression) {
    // Set up book with bid/ask at multiple levels so mid changes as we remove liquidity
    place_buy(book, pool, 100, 150 * PRICE_SCALE, 10);
    place_buy(book, pool, 101, 149 * PRICE_SCALE, 10);
    place_sell(book, pool, 200, 151 * PRICE_SCALE, 10);
    place_sell(book, pool, 201, 152 * PRICE_SCALE, 10);

    PriceImpact pi(200);

    // First event to establish prev_mid
    auto event0 = make_accepted(1, 150 * PRICE_SCALE);
    pi.on_event(event0, book, Side::Buy);

    // Simulate mid price changes by modifying the book between trade events.
    // Remove best ask to shift mid, then observe impact.
    Price initial_mid = book.mid_price();

    // Trade at ask, then remove the ask order to shift mid
    auto trade1 = make_trade(151 * PRICE_SCALE, 10, 1, 1000);
    pi.on_event(trade1, book, Side::Buy);

    // Simulate mid shift: remove best ask, now best ask = 152
    book.cancel_order(200);
    // New mid = (150 + 152) / 2 = 151, was (150 + 151) / 2 = 150.5
    Price new_mid = book.mid_price();
    EXPECT_NE(initial_mid, new_mid);

    // Now trade again — delta_mid should be nonzero
    auto trade2 = make_trade(152 * PRICE_SCALE, 10, 2, 1001);
    pi.on_event(trade2, book, Side::Buy);

    // With actual mid movement, temporary impact should be positive
    EXPECT_GT(pi.avg_temporary_impact_bps(), 0.0);
}

TEST_F(PriceImpactTest, MixedFlowRegression) {
    place_buy(book, pool, 100, 150 * PRICE_SCALE, 100);
    place_sell(book, pool, 200, 151 * PRICE_SCALE, 100);

    PriceImpact pi(200);
    Price base_mid = book.mid_price();

    // First event to establish prev_mid
    auto event0 = make_accepted(1, 150 * PRICE_SCALE);
    pi.on_event(event0, book, Side::Buy);

    // Alternating buys and sells with varying sizes
    for (int i = 0; i < 20; ++i) {
        Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        Quantity qty = static_cast<Quantity>(10 + i);
        Price price = base_mid + (side == Side::Buy ? TICK : -TICK);
        auto trade = make_trade(price, qty, i, 1000 + i);
        pi.on_event(trade, book, side);
    }

    // With mixed flow and varying sizes, should get a valid lambda
    double lambda = pi.kyle_lambda();
    // Lambda should be finite (not NaN) with mixed flow
    EXPECT_FALSE(std::isnan(lambda));
}

TEST_F(PriceImpactTest, JsonOutput) {
    PriceImpact pi(200);
    auto j = pi.to_json();
    EXPECT_TRUE(j.contains("kyle_lambda"));
    EXPECT_TRUE(j.contains("avg_temporary_impact_bps"));
    EXPECT_TRUE(j.contains("avg_permanent_impact_bps"));
    EXPECT_TRUE(j.contains("sample_count"));
}

// ============================================================================
// DepthProfile tests
// ============================================================================

class DepthProfileTest : public ::testing::Test {
protected:
    OrderBook book{MIN_PRICE, MAX_PRICE, TICK, 1000};
    MemoryPool<Order> pool{1000};
};

TEST_F(DepthProfileTest, EmptyBook) {
    DepthProfile dp(5);
    auto event = make_accepted(1, 150 * PRICE_SCALE);
    dp.on_event(event, book);

    EXPECT_TRUE(dp.bid_depth().empty());
    EXPECT_TRUE(dp.ask_depth().empty());
    EXPECT_DOUBLE_EQ(dp.depth_imbalance(), 0.0);
}

TEST_F(DepthProfileTest, SymmetricDepth) {
    place_buy(book, pool, 1, 150 * PRICE_SCALE, 10);
    place_sell(book, pool, 2, 151 * PRICE_SCALE, 10);

    DepthProfile dp(5);
    auto event = make_accepted(3, 150 * PRICE_SCALE);
    dp.on_event(event, book);

    ASSERT_EQ(dp.bid_depth().size(), 1u);
    ASSERT_EQ(dp.ask_depth().size(), 1u);
    EXPECT_EQ(dp.bid_depth()[0], 10u);
    EXPECT_EQ(dp.ask_depth()[0], 10u);
    EXPECT_DOUBLE_EQ(dp.depth_imbalance(), 0.0);
}

TEST_F(DepthProfileTest, AsymmetricDepth) {
    place_buy(book, pool, 1, 150 * PRICE_SCALE, 30);
    place_sell(book, pool, 2, 151 * PRICE_SCALE, 10);

    DepthProfile dp(5);
    auto event = make_accepted(3, 150 * PRICE_SCALE);
    dp.on_event(event, book);

    // (30 - 10) / (30 + 10) = 0.5
    EXPECT_DOUBLE_EQ(dp.depth_imbalance(), 0.5);
}

TEST_F(DepthProfileTest, MultiLevelProfile) {
    place_buy(book, pool, 1, 150 * PRICE_SCALE, 10);
    place_buy(book, pool, 2, 149 * PRICE_SCALE, 20);
    place_buy(book, pool, 3, 148 * PRICE_SCALE, 30);
    place_sell(book, pool, 4, 151 * PRICE_SCALE, 5);
    place_sell(book, pool, 5, 152 * PRICE_SCALE, 15);

    DepthProfile dp(5);
    auto event = make_accepted(6, 150 * PRICE_SCALE);
    dp.on_event(event, book);

    ASSERT_EQ(dp.bid_depth().size(), 3u);
    ASSERT_EQ(dp.ask_depth().size(), 2u);

    // Bid: 10 + 20 + 30 = 60, Ask: 5 + 15 = 20
    // Imbalance: (60 - 20) / (60 + 20) = 0.5
    EXPECT_DOUBLE_EQ(dp.depth_imbalance(), 0.5);
}

TEST_F(DepthProfileTest, JsonOutput) {
    place_buy(book, pool, 1, 150 * PRICE_SCALE, 10);
    place_sell(book, pool, 2, 151 * PRICE_SCALE, 10);

    DepthProfile dp(5);
    auto event = make_accepted(3, 150 * PRICE_SCALE);
    dp.on_event(event, book);

    auto j = dp.to_json();
    EXPECT_TRUE(j.contains("depth_imbalance"));
    EXPECT_TRUE(j.contains("current_bid_depth"));
    EXPECT_TRUE(j.contains("current_ask_depth"));
    EXPECT_TRUE(j.contains("avg_bid_depth"));
    EXPECT_TRUE(j.contains("snapshot_count"));
}

// ============================================================================
// AnalyticsEngine tests
// ============================================================================

class AnalyticsEngineTest : public ::testing::Test {
protected:
    OrderBook book{MIN_PRICE, MAX_PRICE, TICK, 1000};
    MemoryPool<Order> pool{1000};
};

TEST_F(AnalyticsEngineTest, EmptyBookEvents) {
    AnalyticsEngine engine(book);

    auto event = make_accepted(1, 150 * PRICE_SCALE);
    engine.on_event(event);

    EXPECT_EQ(engine.trade_count(), 0u);
}

TEST_F(AnalyticsEngineTest, TradesRecorded) {
    place_buy(book, pool, 100, 150 * PRICE_SCALE, 100);
    place_sell(book, pool, 200, 151 * PRICE_SCALE, 100);

    AnalyticsEngine engine(book);

    // First establish book state
    auto acc = make_accepted(1, 150 * PRICE_SCALE);
    engine.on_event(acc);

    // Trade events
    for (int i = 0; i < 5; ++i) {
        auto trade = make_trade(150 * PRICE_SCALE + TICK, 10, i + 1, 1000 + i);
        engine.on_event(trade);
    }

    EXPECT_EQ(engine.trade_count(), 5u);
}

TEST_F(AnalyticsEngineTest, AllModulesDispatch) {
    place_buy(book, pool, 100, 150 * PRICE_SCALE, 100);
    place_sell(book, pool, 200, 151 * PRICE_SCALE, 100);

    AnalyticsEngine engine(book);

    // Establish state
    auto acc = make_accepted(1, 150 * PRICE_SCALE);
    engine.on_event(acc);

    auto trade = make_trade(150 * PRICE_SCALE + TICK, 10, 1, 1000);
    engine.on_event(trade);

    // Verify all modules received data
    EXPECT_GT(engine.spread().current_spread_bps(), 0.0);
    EXPECT_TRUE(engine.microprice().is_valid());
    EXPECT_EQ(engine.order_flow().sample_count(), 1u);
    EXPECT_EQ(engine.depth().bid_depth().size(), 1u);
}

TEST_F(AnalyticsEngineTest, JsonOutput) {
    place_buy(book, pool, 100, 150 * PRICE_SCALE, 100);
    place_sell(book, pool, 200, 151 * PRICE_SCALE, 100);

    AnalyticsEngine engine(book);

    auto acc = make_accepted(1, 150 * PRICE_SCALE);
    engine.on_event(acc);

    auto trade = make_trade(150 * PRICE_SCALE + TICK, 10, 1, 1000);
    engine.on_event(trade);

    auto j = engine.to_json();
    EXPECT_TRUE(j.contains("spread"));
    EXPECT_TRUE(j.contains("microprice"));
    EXPECT_TRUE(j.contains("order_flow_imbalance"));
    EXPECT_TRUE(j.contains("realized_volatility"));
    EXPECT_TRUE(j.contains("price_impact"));
    EXPECT_TRUE(j.contains("depth_profile"));
    EXPECT_TRUE(j.contains("trade_count"));
}

TEST_F(AnalyticsEngineTest, CsvOutput) {
    place_buy(book, pool, 100, 150 * PRICE_SCALE, 100);
    place_sell(book, pool, 200, 151 * PRICE_SCALE, 100);

    AnalyticsEngine engine(book);

    auto acc = make_accepted(1, 150 * PRICE_SCALE);
    engine.on_event(acc);

    for (int i = 0; i < 3; ++i) {
        auto trade = make_trade(150 * PRICE_SCALE + TICK, 10, i + 1, 1000 + i);
        engine.on_event(trade);
    }

    std::string csv_path = "test_analytics_output.csv";
    engine.write_csv(csv_path);

    // Verify CSV was written
    std::ifstream csv(csv_path);
    ASSERT_TRUE(csv.is_open());

    // Read header
    std::string header;
    std::getline(csv, header);
    EXPECT_NE(header.find("sequence_num"), std::string::npos);
    EXPECT_NE(header.find("trade_price"), std::string::npos);
    EXPECT_NE(header.find("aggressor_side"), std::string::npos);

    // Count data rows
    int row_count = 0;
    std::string line;
    while (std::getline(csv, line)) {
        if (!line.empty()) ++row_count;
    }
    EXPECT_EQ(row_count, 3);

    // Cleanup
    csv.close();
    std::remove(csv_path.c_str());
}

TEST_F(AnalyticsEngineTest, JsonFileOutput) {
    place_buy(book, pool, 100, 150 * PRICE_SCALE, 100);
    place_sell(book, pool, 200, 151 * PRICE_SCALE, 100);

    AnalyticsEngine engine(book);

    auto acc = make_accepted(1, 150 * PRICE_SCALE);
    engine.on_event(acc);

    auto trade = make_trade(150 * PRICE_SCALE + TICK, 10, 1, 1000);
    engine.on_event(trade);

    std::string json_path = "test_analytics_output.json";
    engine.write_json(json_path);

    // Verify JSON was written and is valid
    std::ifstream json_file(json_path);
    ASSERT_TRUE(json_file.is_open());

    nlohmann::json parsed;
    ASSERT_NO_THROW(parsed = nlohmann::json::parse(json_file));

    EXPECT_TRUE(parsed.contains("spread"));
    EXPECT_TRUE(parsed.contains("trade_count"));

    // Cleanup
    json_file.close();
    std::remove(json_path.c_str());
}

TEST_F(AnalyticsEngineTest, LeeReadyTickTest) {
    place_buy(book, pool, 100, 150 * PRICE_SCALE, 100);
    place_sell(book, pool, 200, 151 * PRICE_SCALE, 100);

    AnalyticsEngine engine(book);

    // Establish mid price
    auto acc = make_accepted(1, 150 * PRICE_SCALE);
    engine.on_event(acc);

    Price mid = book.mid_price();

    // Trade above mid => buyer-initiated
    auto buy_trade = make_trade(mid + TICK, 10, 1, 1000);
    engine.on_event(buy_trade);
    EXPECT_GT(engine.order_flow().current_imbalance(), 0.0);

    // Trade below mid => seller-initiated
    auto sell_trade = make_trade(mid - TICK, 10, 2, 1001);
    engine.on_event(sell_trade);
    // Imbalance should decrease (was +1, now should be 0)
    EXPECT_NEAR(engine.order_flow().current_imbalance(), 0.0, 0.01);
}

// ============================================================================
// Integration test: replay-style event sequence
// ============================================================================

TEST(AnalyticsIntegration, ReplayStyleSequence) {
    OrderBook book(MIN_PRICE, MAX_PRICE, TICK, 10000);
    MemoryPool<Order> pool(10000);

    AnalyticsConfig config;
    config.imbalance_window = 50;
    config.vol_tick_window = 20;
    config.impact_regression_window = 50;
    config.depth_max_levels = 5;

    AnalyticsEngine engine(book, config);

    // Build initial book: 5 bid levels, 5 ask levels
    Price base_bid = 150 * PRICE_SCALE;
    Price base_ask = 151 * PRICE_SCALE;

    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 3; ++j) {
            OrderId bid_id = static_cast<OrderId>(100 + i * 10 + j);
            OrderId ask_id = static_cast<OrderId>(200 + i * 10 + j);
            Quantity qty = static_cast<Quantity>(10 + j * 5);

            place_buy(book, pool, bid_id, base_bid - i * PRICE_SCALE, qty);
            place_sell(book, pool, ask_id, base_ask + i * PRICE_SCALE, qty);
        }
    }

    // Dispatch initial book state event
    auto acc = make_accepted(1, base_bid);
    engine.on_event(acc);

    // Simulate 30 trades
    Timestamp ts = 1'000'000'000ULL;
    Price price = base_bid + PRICE_SCALE / 2;  // Start at mid

    for (int i = 0; i < 30; ++i) {
        // Oscillate price slightly
        if (i % 3 == 0) price += TICK;
        if (i % 5 == 0) price -= TICK;

        Quantity qty = static_cast<Quantity>(5 + (i % 7));
        auto trade = make_trade(price, qty, static_cast<uint64_t>(i + 2), ts);
        engine.on_event(trade);

        ts += 100'000'000ULL;  // 100ms between trades
    }

    // Verify analytics are populated
    EXPECT_EQ(engine.trade_count(), 30u);
    EXPECT_GT(engine.spread().avg_spread_bps(), 0.0);
    EXPECT_TRUE(engine.microprice().is_valid());
    EXPECT_EQ(engine.order_flow().sample_count(), 30u);
    EXPECT_GE(engine.volatility().tick_volatility(), 0.0);

    // Depth should reflect multi-level book
    EXPECT_GE(engine.depth().bid_depth().size(), 1u);
    EXPECT_GE(engine.depth().ask_depth().size(), 1u);

    // JSON should have all sections
    auto j = engine.to_json();
    EXPECT_EQ(j.size(), 7u);  // 6 modules + trade_count
}

// ============================================================================
// Edge cases
// ============================================================================

TEST(AnalyticsEdgeCases, SingleSidedBook) {
    OrderBook book(MIN_PRICE, MAX_PRICE, TICK, 1000);
    MemoryPool<Order> pool(1000);

    place_buy(book, pool, 1, 150 * PRICE_SCALE, 10);
    // No ask side

    AnalyticsEngine engine(book);
    auto acc = make_accepted(1, 150 * PRICE_SCALE);
    engine.on_event(acc);

    // Should handle gracefully without crashes
    EXPECT_EQ(engine.spread().current_spread(), -1);
    EXPECT_FALSE(engine.microprice().is_valid());
    EXPECT_DOUBLE_EQ(engine.depth().depth_imbalance(), 1.0);
}

TEST(AnalyticsEdgeCases, ZeroQuantityTrade) {
    OrderBook book(MIN_PRICE, MAX_PRICE, TICK, 1000);
    MemoryPool<Order> pool(1000);
    AnalyticsEngine engine(book);

    auto trade = make_trade(150 * PRICE_SCALE, 0, 1, 1000);
    // Should not crash
    engine.on_event(trade);
    EXPECT_EQ(engine.trade_count(), 1u);
}

TEST(AnalyticsEdgeCases, VeryLargeWindow) {
    OrderFlowImbalance ofi(1000000);
    OrderBook book(MIN_PRICE, MAX_PRICE, TICK, 1000);

    // Just a few samples
    for (int i = 0; i < 5; ++i) {
        auto trade = make_trade(150 * PRICE_SCALE, 10, i);
        ofi.on_event(trade, book, Side::Buy);
    }
    EXPECT_EQ(ofi.sample_count(), 5u);
    EXPECT_DOUBLE_EQ(ofi.current_imbalance(), 1.0);
}
