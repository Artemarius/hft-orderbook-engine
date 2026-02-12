#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <vector>

#include "core/order.h"
#include "core/types.h"
#include "orderbook/flat_order_map.h"
#include "orderbook/memory_pool.h"
#include "orderbook/order_book.h"

namespace hft {
namespace {

// ---------------------------------------------------------------------------
// Test constants — BTC/USDT-like instrument
// ---------------------------------------------------------------------------

constexpr Price TICK = 1'000'000;  // 0.01 USDT in fixed-point
constexpr Price MIN_PRICE = 40'000 * PRICE_SCALE;  // $40,000
constexpr Price MAX_PRICE = 60'000 * PRICE_SCALE;  // $60,000
constexpr size_t MAX_ORDERS = 4096;

/// Helper: create an order with common defaults.
Order make_order(OrderId id, Side side, Price price, Quantity qty) {
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
    o.timestamp = id;  // Simple monotonic timestamp
    o.next = nullptr;
    o.prev = nullptr;
    return o;
}

// ===================================================================
// FlatOrderMap tests
// ===================================================================

TEST(FlatOrderMapTest, InsertAndFind) {
    FlatOrderMap map(64);
    Order o{};
    o.order_id = 42;
    EXPECT_TRUE(map.insert(42, &o));
    EXPECT_EQ(map.find(42), &o);
    EXPECT_EQ(map.size(), 1u);
}

TEST(FlatOrderMapTest, FindMissing) {
    FlatOrderMap map(64);
    EXPECT_EQ(map.find(999), nullptr);
}

TEST(FlatOrderMapTest, DuplicateInsertFails) {
    FlatOrderMap map(64);
    Order o{};
    o.order_id = 1;
    EXPECT_TRUE(map.insert(1, &o));
    EXPECT_FALSE(map.insert(1, &o));
    EXPECT_EQ(map.size(), 1u);
}

TEST(FlatOrderMapTest, EraseExisting) {
    FlatOrderMap map(64);
    Order o{};
    o.order_id = 1;
    map.insert(1, &o);
    EXPECT_TRUE(map.erase(1));
    EXPECT_EQ(map.find(1), nullptr);
    EXPECT_EQ(map.size(), 0u);
}

TEST(FlatOrderMapTest, EraseMissing) {
    FlatOrderMap map(64);
    EXPECT_FALSE(map.erase(999));
}

TEST(FlatOrderMapTest, InsertEraseReinsert) {
    FlatOrderMap map(64);
    Order a{}, b{};
    a.order_id = 1;
    b.order_id = 1;
    map.insert(1, &a);
    map.erase(1);
    EXPECT_TRUE(map.insert(1, &b));
    EXPECT_EQ(map.find(1), &b);
}

TEST(FlatOrderMapTest, ManyInsertions) {
    constexpr size_t N = 1000;
    FlatOrderMap map(N);
    std::vector<Order> orders(N);
    for (size_t i = 0; i < N; ++i) {
        orders[i].order_id = static_cast<OrderId>(i + 1);
        EXPECT_TRUE(map.insert(orders[i].order_id, &orders[i]));
    }
    EXPECT_EQ(map.size(), N);
    for (size_t i = 0; i < N; ++i) {
        EXPECT_EQ(map.find(static_cast<OrderId>(i + 1)), &orders[i]);
    }
}

TEST(FlatOrderMapTest, ManyInsertionsAndDeletions) {
    constexpr size_t N = 500;
    FlatOrderMap map(N);
    std::vector<Order> orders(N);

    // Insert all
    for (size_t i = 0; i < N; ++i) {
        orders[i].order_id = static_cast<OrderId>(i + 1);
        map.insert(orders[i].order_id, &orders[i]);
    }

    // Delete every other
    for (size_t i = 0; i < N; i += 2) {
        EXPECT_TRUE(map.erase(static_cast<OrderId>(i + 1)));
    }
    EXPECT_EQ(map.size(), N / 2);

    // Verify remaining
    for (size_t i = 0; i < N; ++i) {
        Order* found = map.find(static_cast<OrderId>(i + 1));
        if (i % 2 == 0) {
            EXPECT_EQ(found, nullptr);
        } else {
            EXPECT_EQ(found, &orders[i]);
        }
    }
}

TEST(FlatOrderMapTest, ZeroKeyRejected) {
    FlatOrderMap map(64);
    Order o{};
    EXPECT_FALSE(map.insert(0, &o));
    EXPECT_EQ(map.find(0), nullptr);
    EXPECT_FALSE(map.erase(0));
}

TEST(FlatOrderMapTest, Clear) {
    FlatOrderMap map(64);
    Order a{}, b{};
    a.order_id = 1;
    b.order_id = 2;
    map.insert(1, &a);
    map.insert(2, &b);
    EXPECT_EQ(map.size(), 2u);

    map.clear();
    EXPECT_EQ(map.size(), 0u);
    EXPECT_EQ(map.find(1), nullptr);
    EXPECT_EQ(map.find(2), nullptr);
}

// ===================================================================
// OrderBook tests
// ===================================================================

class OrderBookTest : public ::testing::Test {
protected:
    void SetUp() override {
        book_ = new OrderBook(MIN_PRICE, MAX_PRICE, TICK, MAX_ORDERS);
        pool_ = new MemoryPool<Order>(MAX_ORDERS);
    }

    void TearDown() override {
        delete book_;
        delete pool_;
    }

    /// Allocate from pool and initialize.
    Order* alloc_order(OrderId id, Side side, Price price, Quantity qty) {
        Order* o = pool_->allocate();
        *o = make_order(id, side, price, qty);
        return o;
    }

    OrderBook* book_;
    MemoryPool<Order>* pool_;
};

// --- Empty book ---

TEST_F(OrderBookTest, EmptyBook) {
    EXPECT_TRUE(book_->empty());
    EXPECT_EQ(book_->order_count(), 0u);
    EXPECT_EQ(book_->best_bid(), nullptr);
    EXPECT_EQ(book_->best_ask(), nullptr);
    EXPECT_EQ(book_->spread(), -1);
    EXPECT_EQ(book_->mid_price(), 0);
}

// --- Add single order ---

TEST_F(OrderBookTest, AddSingleBid) {
    Price price = 50000 * PRICE_SCALE;
    Order* o = alloc_order(1, Side::Buy, price, 100);

    auto result = book_->add_order(o);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(o->status, OrderStatus::Accepted);
    EXPECT_EQ(book_->order_count(), 1u);

    auto* bb = book_->best_bid();
    ASSERT_NE(bb, nullptr);
    EXPECT_EQ(bb->price, price);
    EXPECT_EQ(bb->order_count, 1u);
    EXPECT_EQ(bb->total_quantity, 100u);

    EXPECT_EQ(book_->best_ask(), nullptr);
}

TEST_F(OrderBookTest, AddSingleAsk) {
    Price price = 50001 * PRICE_SCALE;
    Order* o = alloc_order(1, Side::Sell, price, 200);

    auto result = book_->add_order(o);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(book_->order_count(), 1u);

    auto* ba = book_->best_ask();
    ASSERT_NE(ba, nullptr);
    EXPECT_EQ(ba->price, price);
    EXPECT_EQ(ba->total_quantity, 200u);

    EXPECT_EQ(book_->best_bid(), nullptr);
}

// --- Spread and mid-price ---

TEST_F(OrderBookTest, SpreadAndMidPrice) {
    Price bid_px = 50000 * PRICE_SCALE;
    Price ask_px = 50001 * PRICE_SCALE;
    Order* b = alloc_order(1, Side::Buy, bid_px, 100);
    Order* a = alloc_order(2, Side::Sell, ask_px, 100);

    book_->add_order(b);
    book_->add_order(a);

    EXPECT_EQ(book_->spread(), ask_px - bid_px);
    EXPECT_EQ(book_->mid_price(), (bid_px + ask_px) / 2);
}

// --- Multiple orders at same price (FIFO) ---

TEST_F(OrderBookTest, FIFOWithinPriceLevel) {
    Price px = 50000 * PRICE_SCALE;
    Order* o1 = alloc_order(1, Side::Buy, px, 100);
    Order* o2 = alloc_order(2, Side::Buy, px, 200);
    Order* o3 = alloc_order(3, Side::Buy, px, 300);

    book_->add_order(o1);
    book_->add_order(o2);
    book_->add_order(o3);

    auto* bb = book_->best_bid();
    ASSERT_NE(bb, nullptr);
    EXPECT_EQ(bb->order_count, 3u);
    EXPECT_EQ(bb->total_quantity, 600u);
    EXPECT_EQ(bb->front(), o1);        // FIFO: oldest first
    EXPECT_EQ(bb->front()->next, o2);
}

// --- Multiple price levels ---

TEST_F(OrderBookTest, BestBidIsHighestPrice) {
    Price px1 = 49999 * PRICE_SCALE;
    Price px2 = 50000 * PRICE_SCALE;
    Price px3 = 50001 * PRICE_SCALE;

    Order* o1 = alloc_order(1, Side::Buy, px1, 100);
    Order* o2 = alloc_order(2, Side::Buy, px2, 100);
    Order* o3 = alloc_order(3, Side::Buy, px3, 100);

    book_->add_order(o1);
    book_->add_order(o2);
    book_->add_order(o3);

    EXPECT_EQ(book_->best_bid()->price, px3);
}

TEST_F(OrderBookTest, BestAskIsLowestPrice) {
    Price px1 = 50001 * PRICE_SCALE;
    Price px2 = 50002 * PRICE_SCALE;
    Price px3 = 50003 * PRICE_SCALE;

    Order* o1 = alloc_order(1, Side::Sell, px1, 100);
    Order* o2 = alloc_order(2, Side::Sell, px2, 100);
    Order* o3 = alloc_order(3, Side::Sell, px3, 100);

    book_->add_order(o1);
    book_->add_order(o2);
    book_->add_order(o3);

    EXPECT_EQ(book_->best_ask()->price, px1);
}

// --- Cancel ---

TEST_F(OrderBookTest, CancelExistingOrder) {
    Price px = 50000 * PRICE_SCALE;
    Order* o = alloc_order(1, Side::Buy, px, 100);
    book_->add_order(o);

    auto result = book_->cancel_order(1);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.order, o);
    EXPECT_EQ(o->status, OrderStatus::Cancelled);
    EXPECT_TRUE(book_->empty());
    EXPECT_EQ(book_->best_bid(), nullptr);
}

TEST_F(OrderBookTest, CancelNonExistentOrder) {
    auto result = book_->cancel_order(999);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.order, nullptr);
}

TEST_F(OrderBookTest, CancelBestBidFindsNewBest) {
    Price px1 = 49999 * PRICE_SCALE;
    Price px2 = 50000 * PRICE_SCALE;
    Order* o1 = alloc_order(1, Side::Buy, px1, 100);
    Order* o2 = alloc_order(2, Side::Buy, px2, 100);

    book_->add_order(o1);
    book_->add_order(o2);
    EXPECT_EQ(book_->best_bid()->price, px2);

    book_->cancel_order(2);
    ASSERT_NE(book_->best_bid(), nullptr);
    EXPECT_EQ(book_->best_bid()->price, px1);
}

TEST_F(OrderBookTest, CancelBestAskFindsNewBest) {
    Price px1 = 50001 * PRICE_SCALE;
    Price px2 = 50002 * PRICE_SCALE;
    Order* o1 = alloc_order(1, Side::Sell, px1, 100);
    Order* o2 = alloc_order(2, Side::Sell, px2, 100);

    book_->add_order(o1);
    book_->add_order(o2);
    EXPECT_EQ(book_->best_ask()->price, px1);

    book_->cancel_order(1);
    ASSERT_NE(book_->best_ask(), nullptr);
    EXPECT_EQ(book_->best_ask()->price, px2);
}

TEST_F(OrderBookTest, CancelOneOfManyAtSameLevel) {
    Price px = 50000 * PRICE_SCALE;
    Order* o1 = alloc_order(1, Side::Buy, px, 100);
    Order* o2 = alloc_order(2, Side::Buy, px, 200);
    Order* o3 = alloc_order(3, Side::Buy, px, 300);

    book_->add_order(o1);
    book_->add_order(o2);
    book_->add_order(o3);

    book_->cancel_order(2);  // Cancel middle

    auto* bb = book_->best_bid();
    ASSERT_NE(bb, nullptr);
    EXPECT_EQ(bb->order_count, 2u);
    EXPECT_EQ(bb->total_quantity, 400u);
    EXPECT_EQ(bb->front(), o1);
    EXPECT_EQ(o1->next, o3);
}

// --- Order lookup ---

TEST_F(OrderBookTest, FindOrder) {
    Price px = 50000 * PRICE_SCALE;
    Order* o = alloc_order(42, Side::Buy, px, 100);
    book_->add_order(o);

    EXPECT_EQ(book_->find_order(42), o);
    EXPECT_EQ(book_->find_order(43), nullptr);
}

TEST_F(OrderBookTest, FindAfterCancel) {
    Price px = 50000 * PRICE_SCALE;
    Order* o = alloc_order(1, Side::Buy, px, 100);
    book_->add_order(o);
    book_->cancel_order(1);

    EXPECT_EQ(book_->find_order(1), nullptr);
}

// --- remove_order (matching engine path) ---

TEST_F(OrderBookTest, RemoveOrderDirect) {
    Price px = 50000 * PRICE_SCALE;
    Order* o1 = alloc_order(1, Side::Buy, px, 100);
    Order* o2 = alloc_order(2, Side::Buy, px, 200);

    book_->add_order(o1);
    book_->add_order(o2);

    book_->remove_order(o1);  // As if filled by matching engine

    EXPECT_EQ(book_->order_count(), 1u);
    EXPECT_EQ(book_->find_order(1), nullptr);
    EXPECT_EQ(book_->best_bid()->front(), o2);
}

// --- Validation ---

TEST_F(OrderBookTest, RejectOutOfRangePrice) {
    Price too_low = MIN_PRICE - TICK;
    Price too_high = MAX_PRICE + TICK;
    Order* o1 = alloc_order(1, Side::Buy, too_low, 100);
    Order* o2 = alloc_order(2, Side::Sell, too_high, 100);

    EXPECT_FALSE(book_->add_order(o1).success);
    EXPECT_FALSE(book_->add_order(o2).success);
    EXPECT_TRUE(book_->empty());
}

TEST_F(OrderBookTest, RejectNonTickAlignedPrice) {
    Price misaligned = 50000 * PRICE_SCALE + 1;  // Off by 1 (not tick-aligned)
    Order* o = alloc_order(1, Side::Buy, misaligned, 100);

    EXPECT_FALSE(book_->add_order(o).success);
}

TEST_F(OrderBookTest, RejectDuplicateOrderId) {
    Price px = 50000 * PRICE_SCALE;
    Order* o1 = alloc_order(1, Side::Buy, px, 100);
    Order* o2 = alloc_order(1, Side::Sell, px + TICK, 200);

    EXPECT_TRUE(book_->add_order(o1).success);
    EXPECT_FALSE(book_->add_order(o2).success);
    EXPECT_EQ(book_->order_count(), 1u);
}

// --- Boundary prices ---

TEST_F(OrderBookTest, OrdersAtMinAndMaxPrice) {
    Order* lo = alloc_order(1, Side::Buy, MIN_PRICE, 100);
    Order* hi = alloc_order(2, Side::Sell, MAX_PRICE, 100);

    EXPECT_TRUE(book_->add_order(lo).success);
    EXPECT_TRUE(book_->add_order(hi).success);

    EXPECT_EQ(book_->best_bid()->price, MIN_PRICE);
    EXPECT_EQ(book_->best_ask()->price, MAX_PRICE);
}

// --- Cancel all orders leaves book empty ---

TEST_F(OrderBookTest, CancelAllLeavesEmpty) {
    Price bid_px = 50000 * PRICE_SCALE;
    Price ask_px = 50001 * PRICE_SCALE;

    std::vector<Order*> orders;
    for (OrderId id = 1; id <= 10; ++id) {
        Side side = (id % 2 == 0) ? Side::Buy : Side::Sell;
        Price px = (side == Side::Buy) ? bid_px : ask_px;
        Order* o = alloc_order(id, side, px, 100);
        book_->add_order(o);
        orders.push_back(o);
    }
    EXPECT_EQ(book_->order_count(), 10u);

    for (auto* o : orders) {
        book_->cancel_order(o->order_id);
    }
    EXPECT_TRUE(book_->empty());
    EXPECT_EQ(book_->best_bid(), nullptr);
    EXPECT_EQ(book_->best_ask(), nullptr);
    EXPECT_EQ(book_->spread(), -1);
}

// --- Modify ---

TEST_F(OrderBookTest, ModifyPrice) {
    Price old_px = 50000 * PRICE_SCALE;
    Price new_px = 50001 * PRICE_SCALE;
    Order* o = alloc_order(1, Side::Buy, old_px, 100);
    book_->add_order(o);

    auto result = book_->modify_order(1, new_px, 100, 1000);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.order, o);
    EXPECT_EQ(result.old_price, old_px);
    EXPECT_EQ(result.old_quantity, 100u);
    EXPECT_EQ(o->price, new_px);
    EXPECT_EQ(o->quantity, 100u);
    EXPECT_EQ(o->next, nullptr);
    EXPECT_EQ(o->prev, nullptr);
    // Order is detached — book count should be 0
    EXPECT_EQ(book_->order_count(), 0u);
}

TEST_F(OrderBookTest, ModifyQuantity) {
    Price px = 50000 * PRICE_SCALE;
    Order* o = alloc_order(1, Side::Buy, px, 100);
    book_->add_order(o);

    auto result = book_->modify_order(1, px, 200, 1000);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.old_quantity, 100u);
    EXPECT_EQ(o->quantity, 200u);
    EXPECT_EQ(o->visible_quantity, 200u);
}

TEST_F(OrderBookTest, ModifyNotFound) {
    auto result = book_->modify_order(999, 50000 * PRICE_SCALE, 100, 1000);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.order, nullptr);
}

TEST_F(OrderBookTest, ModifyInvalidPrice) {
    Price px = 50000 * PRICE_SCALE;
    Order* o = alloc_order(1, Side::Buy, px, 100);
    book_->add_order(o);

    // Non-tick-aligned price
    auto result = book_->modify_order(1, px + 1, 100, 1000);
    EXPECT_FALSE(result.success);
    // Order should still be on the book
    EXPECT_EQ(book_->order_count(), 1u);
}

TEST_F(OrderBookTest, ModifyBelowFilled) {
    Price px = 50000 * PRICE_SCALE;
    Order* o = alloc_order(1, Side::Buy, px, 100);
    o->filled_quantity = 50;
    o->status = OrderStatus::PartialFill;
    book_->add_order(o);

    // new_quantity <= filled_quantity — rejected
    auto result = book_->modify_order(1, px, 50, 1000);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(book_->order_count(), 1u);

    // new_quantity < filled_quantity — also rejected
    auto result2 = book_->modify_order(1, px, 30, 1000);
    EXPECT_FALSE(result2.success);
}

TEST_F(OrderBookTest, ModifyUpdatesMap) {
    Price old_px = 50000 * PRICE_SCALE;
    Price new_px = 50001 * PRICE_SCALE;
    Order* o = alloc_order(1, Side::Buy, old_px, 100);
    book_->add_order(o);

    auto result = book_->modify_order(1, new_px, 100, 1000);
    EXPECT_TRUE(result.success);
    // After modify, order is detached — find_order should return nullptr
    EXPECT_EQ(book_->find_order(1), nullptr);
}

TEST_F(OrderBookTest, ModifyUpdatesBestBidAsk) {
    Price px1 = 50000 * PRICE_SCALE;
    Price px2 = 50001 * PRICE_SCALE;
    Order* o1 = alloc_order(1, Side::Buy, px1, 100);
    Order* o2 = alloc_order(2, Side::Buy, px2, 100);
    book_->add_order(o1);
    book_->add_order(o2);
    EXPECT_EQ(book_->best_bid()->price, px2);

    // Modify the best bid — it gets detached, so best bid should drop to px1
    auto result = book_->modify_order(2, px2, 200, 1000);
    EXPECT_TRUE(result.success);
    EXPECT_NE(book_->best_bid(), nullptr);
    EXPECT_EQ(book_->best_bid()->price, px1);
}

// --- Stress: many levels, many orders ---

TEST_F(OrderBookTest, ManyLevelsAndOrders) {
    constexpr int NUM_LEVELS = 100;
    constexpr int ORDERS_PER_LEVEL = 5;
    Price base_bid = 49900 * PRICE_SCALE;
    Price base_ask = 50100 * PRICE_SCALE;
    OrderId next_id = 1;

    // Add bids at 100 levels below mid
    for (int lvl = 0; lvl < NUM_LEVELS; ++lvl) {
        Price px = base_bid + static_cast<Price>(lvl) * TICK;
        for (int j = 0; j < ORDERS_PER_LEVEL; ++j) {
            Order* o = alloc_order(next_id++, Side::Buy, px, 10);
            ASSERT_TRUE(book_->add_order(o).success);
        }
    }

    // Add asks at 100 levels above mid
    for (int lvl = 0; lvl < NUM_LEVELS; ++lvl) {
        Price px = base_ask + static_cast<Price>(lvl) * TICK;
        for (int j = 0; j < ORDERS_PER_LEVEL; ++j) {
            Order* o = alloc_order(next_id++, Side::Sell, px, 10);
            ASSERT_TRUE(book_->add_order(o).success);
        }
    }

    size_t total = static_cast<size_t>(2 * NUM_LEVELS * ORDERS_PER_LEVEL);
    EXPECT_EQ(book_->order_count(), total);

    // Best bid is the highest bid level
    Price expected_best_bid = base_bid + static_cast<Price>(NUM_LEVELS - 1) * TICK;
    EXPECT_EQ(book_->best_bid()->price, expected_best_bid);

    // Best ask is the lowest ask level
    EXPECT_EQ(book_->best_ask()->price, base_ask);

    // Cancel the best bid's orders, verify new best found
    for (int j = 0; j < ORDERS_PER_LEVEL; ++j) {
        OrderId bid_id =
            static_cast<OrderId>((NUM_LEVELS - 1) * ORDERS_PER_LEVEL + j + 1);
        book_->cancel_order(bid_id);
    }
    Price new_best_bid = base_bid + static_cast<Price>(NUM_LEVELS - 2) * TICK;
    ASSERT_NE(book_->best_bid(), nullptr);
    EXPECT_EQ(book_->best_bid()->price, new_best_bid);
}

// ===================================================================
// Zero heap allocation after construction
// ===================================================================

static std::atomic<size_t> g_heap_alloc_count{0};
static bool g_tracking_enabled = false;

}  // namespace
}  // namespace hft

void* operator new(std::size_t size) {
    if (hft::g_tracking_enabled) {
        ++hft::g_heap_alloc_count;
    }
    void* ptr = std::malloc(size);
    if (!ptr) throw std::bad_alloc();
    return ptr;
}
void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }

namespace hft {
namespace {

TEST(OrderBookZeroAllocTest, AddCancelNoHeapAlloc) {
    // Construct everything first (heap alloc allowed here)
    OrderBook book(MIN_PRICE, MAX_PRICE, TICK, 256);
    MemoryPool<Order> pool(256);

    // Pre-allocate orders
    std::vector<Order*> orders;
    for (size_t i = 0; i < 128; ++i) {
        orders.push_back(pool.allocate());
    }

    // Enable tracking
    g_heap_alloc_count = 0;
    g_tracking_enabled = true;

    // Run add/cancel cycles — should be zero-alloc
    Price px = 50000 * PRICE_SCALE;
    for (size_t i = 0; i < 128; ++i) {
        *orders[i] = make_order(static_cast<OrderId>(i + 1), Side::Buy,
                                px + static_cast<Price>(i % 10) * TICK, 100);
        book.add_order(orders[i]);
    }
    for (size_t i = 0; i < 128; ++i) {
        book.cancel_order(static_cast<OrderId>(i + 1));
    }

    g_tracking_enabled = false;

    EXPECT_EQ(g_heap_alloc_count, 0u)
        << "Heap allocations detected during add/cancel operations";
}

}  // namespace
}  // namespace hft
