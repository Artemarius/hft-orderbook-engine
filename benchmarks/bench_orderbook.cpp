#include <benchmark/benchmark.h>

#include "core/order.h"
#include "core/types.h"
#include "orderbook/memory_pool.h"
#include "orderbook/order_book.h"

using namespace hft;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr Price TICK = 1'000'000;  // 0.01 in fixed-point
static constexpr Price MIN_PRICE = 40'000 * PRICE_SCALE;
static constexpr Price MAX_PRICE = 60'000 * PRICE_SCALE;
static constexpr Price MID_PRICE = 50'000 * PRICE_SCALE;

static Order make_order(OrderId id, Side side, Price price, Quantity qty) {
    Order o{};
    o.order_id = id;
    o.participant_id = 1;
    o.side = side;
    o.type = OrderType::Limit;
    o.time_in_force = TimeInForce::GTC;
    o.status = OrderStatus::New;
    o.price = price;
    o.quantity = qty;
    o.visible_quantity = qty;
    o.filled_quantity = 0;
    o.timestamp = id;
    o.next = nullptr;
    o.prev = nullptr;
    return o;
}

// ---------------------------------------------------------------------------
// BM_AddOrder_NoMatch — add orders that don't cross
// ---------------------------------------------------------------------------

static void BM_AddOrder_NoMatch(benchmark::State& state) {
    constexpr size_t POOL_SIZE = 1'000'000;
    OrderBook book(MIN_PRICE, MAX_PRICE, TICK, POOL_SIZE);
    MemoryPool<Order> pool(POOL_SIZE);

    OrderId next_id = 1;
    for (auto _ : state) {
        // Alternate bids and asks spread around mid
        Price px = MID_PRICE - 100 * TICK +
                   static_cast<Price>(next_id % 200) * TICK;
        Side side = (px < MID_PRICE) ? Side::Buy : Side::Sell;

        Order* o = pool.allocate();
        *o = make_order(next_id, side, px, 100);
        auto result = book.add_order(o);
        benchmark::DoNotOptimize(result);
        ++next_id;

        // Periodically drain to avoid exhausting pool
        if (next_id % (POOL_SIZE / 2) == 0) {
            state.PauseTiming();
            for (OrderId id = next_id - POOL_SIZE / 2; id < next_id; ++id) {
                auto cr = book.cancel_order(id);
                if (cr.success) {
                    pool.deallocate(cr.order);
                }
            }
            state.ResumeTiming();
        }
    }
}
BENCHMARK(BM_AddOrder_NoMatch)->MinTime(1.0);

// ---------------------------------------------------------------------------
// BM_CancelOrder — cancel orders by ID
// ---------------------------------------------------------------------------

static void BM_CancelOrder(benchmark::State& state) {
    constexpr size_t N = 100'000;
    OrderBook book(MIN_PRICE, MAX_PRICE, TICK, N * 2);
    MemoryPool<Order> pool(N * 2);

    // Pre-fill the book
    for (size_t i = 0; i < N; ++i) {
        Price px = MID_PRICE - 50 * TICK +
                   static_cast<Price>(i % 100) * TICK;
        Side side = (px < MID_PRICE) ? Side::Buy : Side::Sell;
        Order* o = pool.allocate();
        *o = make_order(static_cast<OrderId>(i + 1), side, px, 100);
        book.add_order(o);
    }

    OrderId cancel_id = 1;
    OrderId add_id = N + 1;
    for (auto _ : state) {
        auto cr = book.cancel_order(cancel_id);
        benchmark::DoNotOptimize(cr);

        // Re-add so the book stays populated
        if (cr.success) {
            Order* o = cr.order;
            Price px = MID_PRICE - 50 * TICK +
                       static_cast<Price>(add_id % 100) * TICK;
            Side side = (px < MID_PRICE) ? Side::Buy : Side::Sell;
            *o = make_order(add_id, side, px, 100);
            book.add_order(o);
            ++add_id;
        }
        ++cancel_id;
    }
}
BENCHMARK(BM_CancelOrder)->MinTime(1.0);

// ---------------------------------------------------------------------------
// BM_AddCancel_Mixed — interleaved add and cancel
// ---------------------------------------------------------------------------

static void BM_AddCancel_Mixed(benchmark::State& state) {
    constexpr size_t POOL_SIZE = 200'000;
    OrderBook book(MIN_PRICE, MAX_PRICE, TICK, POOL_SIZE);
    MemoryPool<Order> pool(POOL_SIZE);

    // Pre-fill half the book
    OrderId next_id = 1;
    for (size_t i = 0; i < POOL_SIZE / 4; ++i) {
        Price px = MID_PRICE - 50 * TICK +
                   static_cast<Price>(next_id % 100) * TICK;
        Side side = (px < MID_PRICE) ? Side::Buy : Side::Sell;
        Order* o = pool.allocate();
        *o = make_order(next_id, side, px, 100);
        book.add_order(o);
        ++next_id;
    }

    OrderId cancel_id = 1;
    size_t iter = 0;
    for (auto _ : state) {
        if (iter % 2 == 0) {
            // Add
            Price px = MID_PRICE - 50 * TICK +
                       static_cast<Price>(next_id % 100) * TICK;
            Side side = (px < MID_PRICE) ? Side::Buy : Side::Sell;
            Order* o = pool.allocate();
            if (o) {
                *o = make_order(next_id, side, px, 100);
                auto result = book.add_order(o);
                benchmark::DoNotOptimize(result);
                ++next_id;
            }
        } else {
            // Cancel
            auto cr = book.cancel_order(cancel_id);
            benchmark::DoNotOptimize(cr);
            if (cr.success) {
                pool.deallocate(cr.order);
            }
            ++cancel_id;
        }
        ++iter;
    }
}
BENCHMARK(BM_AddCancel_Mixed)->MinTime(1.0);

BENCHMARK_MAIN();
