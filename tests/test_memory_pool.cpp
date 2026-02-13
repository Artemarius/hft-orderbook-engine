#include <gtest/gtest.h>

#include <atomic>
#include <vector>

#include "core/order.h"
#include "orderbook/memory_pool.h"

namespace hft {
namespace {

// ---------------------------------------------------------------------------
// Heap allocation detector — overrides global operator new to count
// allocations made AFTER the pool is constructed.
//
// Disabled under AddressSanitizer: ASan intercepts operator new/delete with
// its own allocator, and our raw malloc/free overrides cause
// alloc-dealloc-mismatch errors.
// ---------------------------------------------------------------------------

static std::atomic<size_t> g_heap_alloc_count{0};
static bool g_tracking_enabled = false;

}  // namespace
}  // namespace hft

#if !defined(__SANITIZE_ADDRESS__) && !defined(HFT_ASAN_ENABLED)
// Note: __SANITIZE_ADDRESS__ is defined by GCC when -fsanitize=address is used.
// For Clang, cmake can define HFT_ASAN_ENABLED, or check __has_feature separately.

// Global operator new override — counts allocations when tracking is enabled.
void* operator new(std::size_t size) {
    if (hft::g_tracking_enabled) {
        ++hft::g_heap_alloc_count;
    }
    void* ptr = std::malloc(size);
    if (!ptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }

#endif  // !AddressSanitizer

namespace hft {
namespace {

// ---------------------------------------------------------------------------
// Basic allocation / deallocation
// ---------------------------------------------------------------------------

TEST(MemoryPoolTest, AllocateReturnsNonNull) {
    MemoryPool<Order> pool(16);
    Order* o = pool.allocate();
    ASSERT_NE(o, nullptr);
    pool.deallocate(o);
}

TEST(MemoryPoolTest, AllocateReturnsDistinctPointers) {
    MemoryPool<Order> pool(16);
    Order* a = pool.allocate();
    Order* b = pool.allocate();
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_NE(a, b);
    pool.deallocate(a);
    pool.deallocate(b);
}

TEST(MemoryPoolTest, DeallocateAndReuse) {
    MemoryPool<Order> pool(4);
    Order* a = pool.allocate();
    pool.deallocate(a);
    Order* b = pool.allocate();
    // Freed slot should be reused (LIFO free list → same address)
    EXPECT_EQ(a, b);
    pool.deallocate(b);
}

// ---------------------------------------------------------------------------
// Capacity and exhaustion
// ---------------------------------------------------------------------------

TEST(MemoryPoolTest, CapacityReported) {
    MemoryPool<Order> pool(1024);
    EXPECT_EQ(pool.capacity(), 1024u);
}

TEST(MemoryPoolTest, ExhaustPool) {
    constexpr size_t kCap = 8;
    MemoryPool<Order> pool(kCap);

    std::vector<Order*> ptrs;
    for (size_t i = 0; i < kCap; ++i) {
        Order* o = pool.allocate();
        ASSERT_NE(o, nullptr) << "Allocation " << i << " should succeed";
        ptrs.push_back(o);
    }

    // Pool should now be full
    EXPECT_TRUE(pool.full());
    EXPECT_EQ(pool.size(), kCap);
    EXPECT_EQ(pool.allocate(), nullptr);

    // Free one, then allocate again should succeed
    pool.deallocate(ptrs.back());
    ptrs.pop_back();
    EXPECT_FALSE(pool.full());

    Order* o = pool.allocate();
    EXPECT_NE(o, nullptr);
    ptrs.push_back(o);

    for (auto* p : ptrs) {
        pool.deallocate(p);
    }
}

TEST(MemoryPoolTest, SizeTracking) {
    MemoryPool<Order> pool(8);
    EXPECT_EQ(pool.size(), 0u);
    EXPECT_TRUE(pool.empty());

    Order* a = pool.allocate();
    EXPECT_EQ(pool.size(), 1u);

    Order* b = pool.allocate();
    EXPECT_EQ(pool.size(), 2u);

    pool.deallocate(a);
    EXPECT_EQ(pool.size(), 1u);

    pool.deallocate(b);
    EXPECT_EQ(pool.size(), 0u);
    EXPECT_TRUE(pool.empty());
}

// ---------------------------------------------------------------------------
// High-water mark
// ---------------------------------------------------------------------------

TEST(MemoryPoolTest, HighWaterMark) {
    MemoryPool<Order> pool(16);

    Order* a = pool.allocate();
    Order* b = pool.allocate();
    Order* c = pool.allocate();
    EXPECT_EQ(pool.high_water_mark(), 3u);

    pool.deallocate(c);
    pool.deallocate(b);
    EXPECT_EQ(pool.high_water_mark(), 3u);  // Does not decrease

    Order* d = pool.allocate();
    EXPECT_EQ(pool.high_water_mark(), 3u);  // Still 3 (reused, not new peak)

    Order* e = pool.allocate();
    Order* f = pool.allocate();
    Order* g = pool.allocate();
    EXPECT_EQ(pool.high_water_mark(), 5u);  // New peak: a + d + e + f + g = 5

    pool.deallocate(a);
    pool.deallocate(d);
    pool.deallocate(e);
    pool.deallocate(f);
    pool.deallocate(g);
}

// ---------------------------------------------------------------------------
// Ownership check
// ---------------------------------------------------------------------------

TEST(MemoryPoolTest, OwnsAllocatedPointers) {
    MemoryPool<Order> pool(8);
    Order* a = pool.allocate();
    Order* b = pool.allocate();

    EXPECT_TRUE(pool.owns(a));
    EXPECT_TRUE(pool.owns(b));

    Order stack_order{};
    EXPECT_FALSE(pool.owns(&stack_order));

    pool.deallocate(a);
    pool.deallocate(b);
}

// ---------------------------------------------------------------------------
// Zero heap allocation after construction
// ---------------------------------------------------------------------------

TEST(MemoryPoolTest, ZeroHeapAllocAfterConstruction) {
    MemoryPool<Order> pool(256);

    // Reset counter and enable tracking
    g_heap_alloc_count = 0;
    g_tracking_enabled = true;

    // Run a bunch of alloc/dealloc cycles — none should hit the heap
    constexpr int kCycles = 1000;
    for (int i = 0; i < kCycles; ++i) {
        Order* o = pool.allocate();
        ASSERT_NE(o, nullptr);
        o->order_id = static_cast<OrderId>(i);
        o->price = 50000 * PRICE_SCALE;
        o->quantity = 100;
        o->filled_quantity = 0;
        pool.deallocate(o);
    }

    g_tracking_enabled = false;
    EXPECT_EQ(g_heap_alloc_count, 0u)
        << "Heap allocations detected during pool allocate/deallocate";
}

TEST(MemoryPoolTest, ZeroHeapAllocBulkCycles) {
    MemoryPool<Order> pool(64);

    g_heap_alloc_count = 0;
    g_tracking_enabled = true;

    // Fill the pool, then drain, repeat
    Order* ptrs[64];
    for (int cycle = 0; cycle < 10; ++cycle) {
        for (size_t i = 0; i < 64; ++i) {
            ptrs[i] = pool.allocate();
            ASSERT_NE(ptrs[i], nullptr);
        }
        EXPECT_TRUE(pool.full());
        for (size_t i = 0; i < 64; ++i) {
            pool.deallocate(ptrs[i]);
        }
        EXPECT_TRUE(pool.empty());
    }

    g_tracking_enabled = false;
    EXPECT_EQ(g_heap_alloc_count, 0u)
        << "Heap allocations detected during bulk alloc/dealloc cycles";
}

// ---------------------------------------------------------------------------
// Allocated memory is usable (write + read back)
// ---------------------------------------------------------------------------

TEST(MemoryPoolTest, AllocatedMemoryIsWritable) {
    MemoryPool<Order> pool(4);
    Order* o = pool.allocate();
    ASSERT_NE(o, nullptr);

    // Write all fields
    o->order_id = 42;
    o->participant_id = 7;
    o->side = Side::Buy;
    o->type = OrderType::Limit;
    o->time_in_force = TimeInForce::GTC;
    o->status = OrderStatus::New;
    o->price = 50000 * PRICE_SCALE;
    o->quantity = 100;
    o->visible_quantity = 100;
    o->filled_quantity = 0;
    o->timestamp = 999;
    o->next = nullptr;
    o->prev = nullptr;

    // Read back
    EXPECT_EQ(o->order_id, 42u);
    EXPECT_EQ(o->participant_id, 7u);
    EXPECT_EQ(o->side, Side::Buy);
    EXPECT_EQ(o->price, 50000 * PRICE_SCALE);
    EXPECT_EQ(o->quantity, 100u);

    pool.deallocate(o);
}

}  // namespace
}  // namespace hft
