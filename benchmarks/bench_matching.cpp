#include <benchmark/benchmark.h>

#include "core/order.h"
#include "core/types.h"
#include "matching/matching_engine.h"
#include "orderbook/memory_pool.h"
#include "orderbook/order_book.h"

using namespace hft;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr Price TICK = 1'000'000;  // 0.01 in fixed-point
static constexpr Price MIN_PRICE = 40'000 * PRICE_SCALE;
static constexpr Price MAX_PRICE = 60'000 * PRICE_SCALE;
static constexpr Price MID = 50'000 * PRICE_SCALE;
static constexpr size_t POOL_SIZE = 1'000'000;

static Order make_order(OrderId id, Side side, OrderType type,
                        Price price, Quantity qty) {
    Order o{};
    o.order_id = id;
    o.participant_id = 1;
    o.side = side;
    o.type = type;
    o.time_in_force = TimeInForce::GTC;
    o.status = OrderStatus::New;
    o.price = price;
    o.quantity = qty;
    o.visible_quantity = qty;
    o.iceberg_slice_qty = 0;
    o.filled_quantity = 0;
    o.timestamp = id;
    o.next = nullptr;
    o.prev = nullptr;
    return o;
}

// ---------------------------------------------------------------------------
// Helper: place a sentinel order on the ask side so that
// update_best_ask_after_remove terminates in 1 step instead of
// scanning ~1M empty levels to the end of the flat array.
// This isolates the matching cost from O(N) best-price scan overhead.
// ---------------------------------------------------------------------------

static constexpr OrderId SENTINEL_ID = UINT64_MAX;
static constexpr Price SENTINEL_ASK = MID + 1000 * TICK;  // Far from MID

static void place_ask_sentinel(OrderBook& book, MemoryPool<Order>& pool) {
    Order* s = pool.allocate();
    *s = make_order(SENTINEL_ID, Side::Sell, OrderType::Limit,
                    SENTINEL_ASK, 1'000'000);
    book.add_order(s);
}

static constexpr OrderId SENTINEL_BID_ID = UINT64_MAX - 1;
static constexpr Price SENTINEL_BID = MID - 1000 * TICK;

static void place_bid_sentinel(OrderBook& book, MemoryPool<Order>& pool) {
    Order* s = pool.allocate();
    *s = make_order(SENTINEL_BID_ID, Side::Buy, OrderType::Limit,
                    SENTINEL_BID, 1'000'000);
    book.add_order(s);
}

// ---------------------------------------------------------------------------
// BM_LimitMatch_SingleLevel — aggressive matches 1 resting order
// Target: < 200ns
// ---------------------------------------------------------------------------

static void BM_LimitMatch_SingleLevel(benchmark::State& state) {
    MemoryPool<Order> pool(POOL_SIZE);
    OrderBook book(MIN_PRICE, MAX_PRICE, TICK, POOL_SIZE);
    MatchingEngine engine(book, pool, SelfTradePreventionMode::None);

    // Sentinel ask so best-ask scan stops quickly after the matched level empties
    place_ask_sentinel(book, pool);

    OrderId next_id = 1;
    for (auto _ : state) {
        // Place a resting sell
        Order* sell = pool.allocate();
        *sell = make_order(next_id++, Side::Sell, OrderType::Limit, MID, 100);
        book.add_order(sell);

        // Match with aggressive buy
        Order* buy = pool.allocate();
        *buy = make_order(next_id++, Side::Buy, OrderType::Limit, MID, 100);
        auto result = engine.submit_order(buy);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_LimitMatch_SingleLevel)->MinTime(1.0);

// ---------------------------------------------------------------------------
// BM_LimitMatch_MultiLevel — aggressive walks 5 price levels
// ---------------------------------------------------------------------------

static void BM_LimitMatch_MultiLevel(benchmark::State& state) {
    MemoryPool<Order> pool(POOL_SIZE);
    OrderBook book(MIN_PRICE, MAX_PRICE, TICK, POOL_SIZE);
    MatchingEngine engine(book, pool, SelfTradePreventionMode::None);

    place_ask_sentinel(book, pool);

    OrderId next_id = 1;
    for (auto _ : state) {
        // Place 5 resting sells at increasing prices
        for (int i = 0; i < 5; ++i) {
            Order* sell = pool.allocate();
            *sell = make_order(next_id++, Side::Sell, OrderType::Limit,
                               MID + static_cast<Price>(i) * TICK, 100);
            book.add_order(sell);
        }

        // Match with aggressive buy that crosses all 5 levels
        Order* buy = pool.allocate();
        *buy = make_order(next_id++, Side::Buy, OrderType::Limit,
                          MID + 4 * TICK, 500);
        auto result = engine.submit_order(buy);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_LimitMatch_MultiLevel)->MinTime(1.0);

// ---------------------------------------------------------------------------
// BM_LimitNoMatch_Rest — submit order that doesn't cross (baseline)
// ---------------------------------------------------------------------------

static void BM_LimitNoMatch_Rest(benchmark::State& state) {
    MemoryPool<Order> pool(POOL_SIZE);
    OrderBook book(MIN_PRICE, MAX_PRICE, TICK, POOL_SIZE);
    MatchingEngine engine(book, pool, SelfTradePreventionMode::None);

    // Pre-place some sells above MID
    OrderId next_id = 1;
    for (int i = 0; i < 10; ++i) {
        Order* sell = pool.allocate();
        *sell = make_order(next_id++, Side::Sell, OrderType::Limit,
                           MID + static_cast<Price>(10 + i) * TICK, 100);
        book.add_order(sell);
    }

    for (auto _ : state) {
        // Buy below the best ask — no match, rests on book
        Order* buy = pool.allocate();
        *buy = make_order(next_id++, Side::Buy, OrderType::Limit,
                          MID, 100);
        auto result = engine.submit_order(buy);
        benchmark::DoNotOptimize(result);

        // Clean up to avoid exhausting pool
        state.PauseTiming();
        book.cancel_order(next_id - 1);
        pool.deallocate(buy);
        state.ResumeTiming();
    }
}
BENCHMARK(BM_LimitNoMatch_Rest)->MinTime(1.0);

// ---------------------------------------------------------------------------
// BM_MarketOrder — market order fills against resting liquidity
// ---------------------------------------------------------------------------

static void BM_MarketOrder(benchmark::State& state) {
    MemoryPool<Order> pool(POOL_SIZE);
    OrderBook book(MIN_PRICE, MAX_PRICE, TICK, POOL_SIZE);
    MatchingEngine engine(book, pool, SelfTradePreventionMode::None);

    place_ask_sentinel(book, pool);

    OrderId next_id = 1;
    for (auto _ : state) {
        // Place resting sell
        Order* sell = pool.allocate();
        *sell = make_order(next_id++, Side::Sell, OrderType::Limit, MID, 100);
        book.add_order(sell);

        // Market buy
        Order* buy = pool.allocate();
        *buy = make_order(next_id++, Side::Buy, OrderType::Market, 0, 100);
        auto result = engine.submit_order(buy);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_MarketOrder)->MinTime(1.0);

// ---------------------------------------------------------------------------
// BM_IOC_Match — IOC order that matches
// ---------------------------------------------------------------------------

static void BM_IOC_Match(benchmark::State& state) {
    MemoryPool<Order> pool(POOL_SIZE);
    OrderBook book(MIN_PRICE, MAX_PRICE, TICK, POOL_SIZE);
    MatchingEngine engine(book, pool, SelfTradePreventionMode::None);

    place_ask_sentinel(book, pool);

    OrderId next_id = 1;
    for (auto _ : state) {
        Order* sell = pool.allocate();
        *sell = make_order(next_id++, Side::Sell, OrderType::Limit, MID, 100);
        book.add_order(sell);

        Order* buy = pool.allocate();
        *buy = make_order(next_id++, Side::Buy, OrderType::IOC, MID, 100);
        auto result = engine.submit_order(buy);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_IOC_Match)->MinTime(1.0);

// ---------------------------------------------------------------------------
// BM_FOK_Accept — FOK order that can be fully filled
// ---------------------------------------------------------------------------

static void BM_FOK_Accept(benchmark::State& state) {
    MemoryPool<Order> pool(POOL_SIZE);
    OrderBook book(MIN_PRICE, MAX_PRICE, TICK, POOL_SIZE);
    MatchingEngine engine(book, pool, SelfTradePreventionMode::None);

    place_ask_sentinel(book, pool);

    OrderId next_id = 1;
    for (auto _ : state) {
        Order* sell = pool.allocate();
        *sell = make_order(next_id++, Side::Sell, OrderType::Limit, MID, 100);
        book.add_order(sell);

        Order* buy = pool.allocate();
        *buy = make_order(next_id++, Side::Buy, OrderType::FOK, MID, 100);
        auto result = engine.submit_order(buy);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_FOK_Accept)->MinTime(1.0);

// ---------------------------------------------------------------------------
// BM_FOK_Reject — FOK order rejected due to insufficient liquidity
// ---------------------------------------------------------------------------

static void BM_FOK_Reject(benchmark::State& state) {
    MemoryPool<Order> pool(POOL_SIZE);
    OrderBook book(MIN_PRICE, MAX_PRICE, TICK, POOL_SIZE);
    MatchingEngine engine(book, pool, SelfTradePreventionMode::None);

    // Pre-place some liquidity — but not enough
    OrderId next_id = 1;
    Order* sell = pool.allocate();
    *sell = make_order(next_id++, Side::Sell, OrderType::Limit, MID, 50);
    book.add_order(sell);

    for (auto _ : state) {
        // FOK wants 100 but only 50 available — rejected
        Order* buy = pool.allocate();
        *buy = make_order(next_id++, Side::Buy, OrderType::FOK, MID, 100);
        auto result = engine.submit_order(buy);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_FOK_Reject)->MinTime(1.0);

// ---------------------------------------------------------------------------
// BM_ModifyOrder_SamePrice — modify quantity only (no price change)
// Target: ~200-400ns (similar to cancel + add)
// ---------------------------------------------------------------------------

static void BM_ModifyOrder_SamePrice(benchmark::State& state) {
    MemoryPool<Order> pool(POOL_SIZE);
    OrderBook book(MIN_PRICE, MAX_PRICE, TICK, POOL_SIZE);
    MatchingEngine engine(book, pool, SelfTradePreventionMode::None);

    place_ask_sentinel(book, pool);

    OrderId next_id = 1;
    // Place a resting sell far above MID
    Order* sell = pool.allocate();
    *sell = make_order(next_id++, Side::Sell, OrderType::Limit,
                       MID + 50 * TICK, 1'000'000);
    book.add_order(sell);

    // Place initial buy that we'll keep modifying
    Order* buy = pool.allocate();
    *buy = make_order(next_id++, Side::Buy, OrderType::Limit, MID, 100);
    book.add_order(buy);
    OrderId modify_id = buy->order_id;

    Quantity qty = 100;
    for (auto _ : state) {
        qty = (qty == 100) ? 200 : 100;
        auto result = engine.modify_order(modify_id, MID, qty, next_id++);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_ModifyOrder_SamePrice)->MinTime(1.0);

// ---------------------------------------------------------------------------
// BM_ModifyOrder_NewPrice — price change, no cross
// ---------------------------------------------------------------------------

static void BM_ModifyOrder_NewPrice(benchmark::State& state) {
    MemoryPool<Order> pool(POOL_SIZE);
    OrderBook book(MIN_PRICE, MAX_PRICE, TICK, POOL_SIZE);
    MatchingEngine engine(book, pool, SelfTradePreventionMode::None);

    place_ask_sentinel(book, pool);

    OrderId next_id = 1;
    // Place a resting sell far above MID
    Order* sell = pool.allocate();
    *sell = make_order(next_id++, Side::Sell, OrderType::Limit,
                       MID + 50 * TICK, 1'000'000);
    book.add_order(sell);

    // Place initial buy
    Order* buy = pool.allocate();
    *buy = make_order(next_id++, Side::Buy, OrderType::Limit, MID, 100);
    book.add_order(buy);
    OrderId modify_id = buy->order_id;

    bool toggle = false;
    for (auto _ : state) {
        Price new_price = toggle ? MID : (MID - TICK);
        toggle = !toggle;
        auto result = engine.modify_order(modify_id, new_price, 100, next_id++);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_ModifyOrder_NewPrice)->MinTime(1.0);

// ---------------------------------------------------------------------------
// BM_ModifyOrder_Crossing — price change that triggers a match
// ---------------------------------------------------------------------------

static void BM_ModifyOrder_Crossing(benchmark::State& state) {
    MemoryPool<Order> pool(POOL_SIZE);
    OrderBook book(MIN_PRICE, MAX_PRICE, TICK, POOL_SIZE);
    MatchingEngine engine(book, pool, SelfTradePreventionMode::None);

    place_ask_sentinel(book, pool);
    place_bid_sentinel(book, pool);

    OrderId next_id = 1;
    for (auto _ : state) {
        // Place a resting sell at MID
        Order* sell = pool.allocate();
        *sell = make_order(next_id++, Side::Sell, OrderType::Limit, MID, 100);
        book.add_order(sell);

        // Place a buy below the sell
        Order* buy = pool.allocate();
        *buy = make_order(next_id++, Side::Buy, OrderType::Limit,
                          MID - TICK, 100);
        book.add_order(buy);
        OrderId modify_id = buy->order_id;

        // Modify the buy to cross the sell
        auto result = engine.modify_order(modify_id, MID, 100, next_id++);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_ModifyOrder_Crossing)->MinTime(1.0);

BENCHMARK_MAIN();
