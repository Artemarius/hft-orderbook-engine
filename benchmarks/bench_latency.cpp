/// @file bench_latency.cpp
/// @brief Custom percentile latency harness for HFT order book engine.
///
/// Measures per-operation latency using rdtsc and reports p50/p90/p99/p99.9/max.
/// Standalone executable — does NOT use Google Benchmark.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include "core/order.h"
#include "core/types.h"
#include "matching/matching_engine.h"
#include "orderbook/memory_pool.h"
#include "orderbook/order_book.h"
#include "transport/message.h"
#include "transport/spsc_ring_buffer.h"
#include "utils/clock.h"
#include "utils/latency_histogram.h"

using namespace hft;

// ---------------------------------------------------------------------------
// Constants (same as existing benchmarks for consistency)
// ---------------------------------------------------------------------------

static constexpr Price TICK = 1'000'000;  // 0.01 in fixed-point
static constexpr Price MIN_PRICE = 40'000 * PRICE_SCALE;
static constexpr Price MAX_PRICE = 60'000 * PRICE_SCALE;
static constexpr Price MID = 50'000 * PRICE_SCALE;
static constexpr size_t POOL_SIZE = 1'000'000;

// Sentinel far from MID to avoid O(N) best-price scans
static constexpr OrderId SENTINEL_ASK_ID = UINT64_MAX;
static constexpr OrderId SENTINEL_BID_ID = UINT64_MAX - 1;
static constexpr Price SENTINEL_ASK = MID + 1000 * TICK;
static constexpr Price SENTINEL_BID = MID - 1000 * TICK;

// ---------------------------------------------------------------------------
// Prevent dead-code elimination (MSVC-safe)
// ---------------------------------------------------------------------------

template <typename T>
static void do_not_optimize(const T& val) {
#ifdef _MSC_VER
    // MSVC: use volatile pointer trick
    const volatile T* p = &val;
    (void)p;
#else
    asm volatile("" : : "g"(val) : "memory");
#endif
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

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

static void place_sentinel(OrderBook& book, MemoryPool<Order>& pool,
                           OrderId id, Side side, Price price) {
    Order* s = pool.allocate();
    *s = make_order(id, side, OrderType::Limit, price, 1'000'000);
    book.add_order(s);
}

static void print_header(const char* name, size_t samples) {
    std::cout << "\n=== " << name << " ("
              << std::fixed << std::setprecision(0)
              << static_cast<double>(samples) << " samples) ===\n";
}

static void print_stat(const char* label, double ns,
                       const char* target_label = nullptr,
                       double target_ns = 0.0) {
    std::cout << "  " << std::left << std::setw(7) << label << ": "
              << std::right << std::setw(8) << std::fixed << std::setprecision(1)
              << ns << " ns";
    if (target_label) {
        bool pass = ns <= target_ns;
        std::cout << "    [TARGET: " << target_label << "]  "
                  << (pass ? "PASS" : "MISS");
    }
    std::cout << "\n";
}

static void print_stat_with_target(const char* label, double ns,
                                   double target_ns) {
    char target_buf[32];
    std::snprintf(target_buf, sizeof(target_buf), "< %d ns",
                  static_cast<int>(target_ns));
    print_stat(label, ns, target_buf, target_ns);
}

static void print_stats(const char* name, const LatencyStats& stats,
                        double target_p50 = 0.0, double target_p99 = 0.0) {
    print_header(name, stats.sample_count);

    print_stat("min", stats.min_ns);

    if (target_p50 > 0)
        print_stat_with_target("p50", stats.p50_ns, target_p50);
    else
        print_stat("p50", stats.p50_ns);

    print_stat("p90", stats.p90_ns);

    if (target_p99 > 0)
        print_stat_with_target("p99", stats.p99_ns, target_p99);
    else
        print_stat("p99", stats.p99_ns);

    print_stat("p99.9", stats.p99_9_ns);
    print_stat("max", stats.max_ns);
    print_stat("mean", stats.mean_ns);
}

// ---------------------------------------------------------------------------
// Pin thread and elevate priority (Windows)
// ---------------------------------------------------------------------------

static void setup_thread_affinity() {
#ifdef _WIN32
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadAffinityMask(GetCurrentThread(), 1);  // Pin to core 0
    std::cout << "Thread pinned to core 0, process priority elevated.\n";
#else
    std::cout << "Thread affinity: not configured (non-Windows).\n";
#endif
}

// ---------------------------------------------------------------------------
// Benchmark 1: AddOrder (no match)
// ---------------------------------------------------------------------------

// Overhead of rdtsc_start/rdtsc_end pair (set once at startup)
static uint64_t g_rdtsc_overhead = 0;

static void bench_add_order_no_match(size_t iterations, double tsc_freq) {
    OrderBook book(MIN_PRICE, MAX_PRICE, TICK, POOL_SIZE);
    MemoryPool<Order> pool(POOL_SIZE);
    LatencyHistogram hist(iterations);
    hist.set_tsc_frequency(tsc_freq);
    hist.set_overhead(g_rdtsc_overhead);
    hist.set_overhead(g_rdtsc_overhead);

    OrderId next_id = 1;

    // Warmup
    for (size_t i = 0; i < 10'000; ++i) {
        Price px = MID - 100 * TICK + static_cast<Price>(next_id % 200) * TICK;
        Side side = (px < MID) ? Side::Buy : Side::Sell;
        Order* o = pool.allocate();
        *o = make_order(next_id++, side, OrderType::Limit, px, 100);
        book.add_order(o);
    }
    // Drain warmup orders
    for (OrderId id = 1; id < next_id; ++id) {
        auto cr = book.cancel_order(id);
        if (cr.success) pool.deallocate(cr.order);
    }
    next_id = 1;

    // Measurement
    for (size_t i = 0; i < iterations; ++i) {
        Price px = MID - 100 * TICK + static_cast<Price>(next_id % 200) * TICK;
        Side side = (px < MID) ? Side::Buy : Side::Sell;
        Order* o = pool.allocate();
        *o = make_order(next_id, side, OrderType::Limit, px, 100);

        uint64_t t0 = rdtsc_start();
        auto result = book.add_order(o);
        uint64_t t1 = rdtsc_end();

        do_not_optimize(result);
        hist.record(t1 - t0);
        ++next_id;

        // Periodic drain to avoid exhausting pool
        if (next_id % (POOL_SIZE / 2) == 0) {
            for (OrderId id = next_id - POOL_SIZE / 2; id < next_id; ++id) {
                auto cr = book.cancel_order(id);
                if (cr.success) pool.deallocate(cr.order);
            }
        }
    }

    auto stats = hist.compute();
    print_stats("AddOrder_NoMatch", stats, 100.0, 500.0);
}

// ---------------------------------------------------------------------------
// Benchmark 2: CancelOrder
// ---------------------------------------------------------------------------

static void bench_cancel_order(size_t iterations, double tsc_freq) {
    constexpr size_t N = 100'000;
    OrderBook book(MIN_PRICE, MAX_PRICE, TICK, N * 2);
    MemoryPool<Order> pool(N * 2);
    LatencyHistogram hist(iterations);
    hist.set_tsc_frequency(tsc_freq);
    hist.set_overhead(g_rdtsc_overhead);

    // Pre-fill the book
    for (size_t i = 0; i < N; ++i) {
        Price px = MID - 50 * TICK + static_cast<Price>(i % 100) * TICK;
        Side side = (px < MID) ? Side::Buy : Side::Sell;
        Order* o = pool.allocate();
        *o = make_order(static_cast<OrderId>(i + 1), side, OrderType::Limit, px, 100);
        book.add_order(o);
    }

    // Warmup
    OrderId cancel_id = 1;
    OrderId add_id = N + 1;
    for (size_t i = 0; i < 10'000; ++i) {
        auto cr = book.cancel_order(cancel_id);
        if (cr.success) {
            Order* o = cr.order;
            Price px = MID - 50 * TICK + static_cast<Price>(add_id % 100) * TICK;
            Side side = (px < MID) ? Side::Buy : Side::Sell;
            *o = make_order(add_id, side, OrderType::Limit, px, 100);
            book.add_order(o);
            ++add_id;
        }
        ++cancel_id;
    }

    // Measurement
    for (size_t i = 0; i < iterations; ++i) {
        uint64_t t0 = rdtsc_start();
        auto cr = book.cancel_order(cancel_id);
        uint64_t t1 = rdtsc_end();

        do_not_optimize(cr);
        hist.record(t1 - t0);

        // Re-add so the book stays populated
        if (cr.success) {
            Order* o = cr.order;
            Price px = MID - 50 * TICK + static_cast<Price>(add_id % 100) * TICK;
            Side side = (px < MID) ? Side::Buy : Side::Sell;
            *o = make_order(add_id, side, OrderType::Limit, px, 100);
            book.add_order(o);
            ++add_id;
        }
        ++cancel_id;
    }

    auto stats = hist.compute();
    print_stats("CancelOrder", stats, 50.0, 200.0);
}

// ---------------------------------------------------------------------------
// Benchmark 3: Match single level
// ---------------------------------------------------------------------------

static void bench_match_single_level(size_t iterations, double tsc_freq) {
    MemoryPool<Order> pool(POOL_SIZE);
    OrderBook book(MIN_PRICE, MAX_PRICE, TICK, POOL_SIZE);
    MatchingEngine engine(book, pool, SelfTradePreventionMode::None);
    LatencyHistogram hist(iterations);
    hist.set_tsc_frequency(tsc_freq);
    hist.set_overhead(g_rdtsc_overhead);

    place_sentinel(book, pool, SENTINEL_ASK_ID, Side::Sell, SENTINEL_ASK);

    OrderId next_id = 1;

    // Warmup
    for (size_t i = 0; i < 10'000; ++i) {
        Order* sell = pool.allocate();
        *sell = make_order(next_id++, Side::Sell, OrderType::Limit, MID, 100);
        book.add_order(sell);

        Order* buy = pool.allocate();
        *buy = make_order(next_id++, Side::Buy, OrderType::Limit, MID, 100);
        (void)engine.submit_order(buy);
    }

    // Measurement
    for (size_t i = 0; i < iterations; ++i) {
        Order* sell = pool.allocate();
        *sell = make_order(next_id++, Side::Sell, OrderType::Limit, MID, 100);
        book.add_order(sell);

        Order* buy = pool.allocate();
        *buy = make_order(next_id++, Side::Buy, OrderType::Limit, MID, 100);

        uint64_t t0 = rdtsc_start();
        auto result = engine.submit_order(buy);
        uint64_t t1 = rdtsc_end();

        do_not_optimize(result);
        hist.record(t1 - t0);
    }

    auto stats = hist.compute();
    print_stats("Match_SingleLevel", stats, 200.0, 1000.0);
}

// ---------------------------------------------------------------------------
// Benchmark 4: Match multi-level (5 levels)
// ---------------------------------------------------------------------------

static void bench_match_multi_level(size_t iterations, double tsc_freq) {
    MemoryPool<Order> pool(POOL_SIZE);
    OrderBook book(MIN_PRICE, MAX_PRICE, TICK, POOL_SIZE);
    MatchingEngine engine(book, pool, SelfTradePreventionMode::None);
    LatencyHistogram hist(iterations);
    hist.set_tsc_frequency(tsc_freq);
    hist.set_overhead(g_rdtsc_overhead);

    place_sentinel(book, pool, SENTINEL_ASK_ID, Side::Sell, SENTINEL_ASK);

    OrderId next_id = 1;

    // Warmup
    for (size_t i = 0; i < 2'000; ++i) {
        for (int j = 0; j < 5; ++j) {
            Order* sell = pool.allocate();
            *sell = make_order(next_id++, Side::Sell, OrderType::Limit,
                               MID + static_cast<Price>(j) * TICK, 100);
            book.add_order(sell);
        }
        Order* buy = pool.allocate();
        *buy = make_order(next_id++, Side::Buy, OrderType::Limit,
                          MID + 4 * TICK, 500);
        (void)engine.submit_order(buy);
    }

    // Measurement
    for (size_t i = 0; i < iterations; ++i) {
        for (int j = 0; j < 5; ++j) {
            Order* sell = pool.allocate();
            *sell = make_order(next_id++, Side::Sell, OrderType::Limit,
                               MID + static_cast<Price>(j) * TICK, 100);
            book.add_order(sell);
        }

        Order* buy = pool.allocate();
        *buy = make_order(next_id++, Side::Buy, OrderType::Limit,
                          MID + 4 * TICK, 500);

        uint64_t t0 = rdtsc_start();
        auto result = engine.submit_order(buy);
        uint64_t t1 = rdtsc_end();

        do_not_optimize(result);
        hist.record(t1 - t0);
    }

    auto stats = hist.compute();
    print_stats("Match_MultiLevel", stats, 500.0, 2000.0);
}

// ---------------------------------------------------------------------------
// Benchmark 5: SPSC push+pop round trip
// ---------------------------------------------------------------------------

static void bench_spsc_push_pop(size_t iterations, double tsc_freq) {
    SPSCRingBuffer<uint64_t, 1024> rb;
    LatencyHistogram hist(iterations);
    hist.set_tsc_frequency(tsc_freq);
    hist.set_overhead(g_rdtsc_overhead);

    uint64_t val = 0;

    // Warmup
    for (size_t i = 0; i < 10'000; ++i) {
        (void)rb.try_push(val);
        (void)rb.try_pop(val);
    }

    // Measurement
    for (size_t i = 0; i < iterations; ++i) {
        uint64_t t0 = rdtsc_start();
        (void)rb.try_push(val);
        (void)rb.try_pop(val);
        uint64_t t1 = rdtsc_end();

        do_not_optimize(val);
        hist.record(t1 - t0);
    }

    auto stats = hist.compute();
    print_stats("SPSC_PushPop", stats, 20.0, 50.0);
}

// ---------------------------------------------------------------------------
// Benchmark 6: Sustained throughput (mixed workload)
// ---------------------------------------------------------------------------

static void bench_throughput_sustained(size_t iterations, double /*tsc_freq*/) {
    constexpr size_t TP_POOL_SIZE = 2'000'000;
    OrderBook book(MIN_PRICE, MAX_PRICE, TICK, TP_POOL_SIZE);
    MemoryPool<Order> pool(TP_POOL_SIZE);
    MatchingEngine engine(book, pool, SelfTradePreventionMode::None);

    place_sentinel(book, pool, SENTINEL_ASK_ID, Side::Sell, SENTINEL_ASK);
    place_sentinel(book, pool, SENTINEL_BID_ID, Side::Buy, SENTINEL_BID);

    // Build initial book depth
    OrderId next_id = 1;
    for (size_t i = 0; i < 10'000; ++i) {
        Order* o = pool.allocate();
        Price offset = static_cast<Price>(i % 200) * TICK;
        Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        Price px = (side == Side::Buy) ? MID - 10 * TICK - offset
                                       : MID + 10 * TICK + offset;
        *o = make_order(next_id++, side, OrderType::Limit, px, 100);
        (void)engine.submit_order(o);
    }

    // Deterministic LCG for varied but reproducible operations
    uint64_t rng = 12345;
    auto next_rng = [&rng]() -> uint64_t {
        rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
        return rng;
    };

    auto start = std::chrono::steady_clock::now();

    size_t completed = 0;
    for (size_t i = 0; i < iterations; ++i) {
        uint64_t r = next_rng();
        unsigned op = static_cast<unsigned>(r % 10);

        if (op < 6) {
            // 60%: add order (away from mid — no match)
            Order* o = pool.allocate();
            if (!o) continue;
            Price offset = static_cast<Price>((r >> 8) % 200) * TICK;
            Side side = ((r >> 16) & 1) ? Side::Buy : Side::Sell;
            Price px = (side == Side::Buy) ? MID - 10 * TICK - offset
                                           : MID + 10 * TICK + offset;
            *o = make_order(next_id++, side, OrderType::Limit, px, 100);
            (void)engine.submit_order(o);
        } else if (op < 9) {
            // 30%: cancel recent order
            if (next_id > 1000) {
                OrderId cancel_id = next_id - static_cast<OrderId>(r % 1000) - 1;
                (void)engine.cancel_order(cancel_id);
            }
        } else {
            // 10%: aggressive order that crosses
            Order* o = pool.allocate();
            if (!o) continue;
            Side side = ((r >> 16) & 1) ? Side::Buy : Side::Sell;
            Price px = (side == Side::Buy) ? MID + 5 * TICK : MID - 5 * TICK;
            *o = make_order(next_id++, side, OrderType::Limit, px, 50);
            (void)engine.submit_order(o);
        }
        ++completed;
    }

    auto end = std::chrono::steady_clock::now();
    double elapsed_ns = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    double msgs_per_sec = static_cast<double>(completed) / (elapsed_ns / 1e9);

    std::cout << "\n=== Throughput_Sustained (" << completed << " operations) ===\n";
    std::cout << "  Elapsed:    " << std::fixed << std::setprecision(1)
              << (elapsed_ns / 1e6) << " ms\n";
    std::cout << "  Throughput: " << std::fixed << std::setprecision(2)
              << (msgs_per_sec / 1e6) << " M msgs/s";
    if (msgs_per_sec >= 5'000'000.0)
        std::cout << "    [TARGET: > 5M msgs/s]  PASS";
    else
        std::cout << "    [TARGET: > 5M msgs/s]  MISS";
    std::cout << "\n";
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

static size_t parse_iterations(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], "--iterations=", 13) == 0) {
            return static_cast<size_t>(std::atoll(argv[i] + 13));
        }
        if (std::strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
            return static_cast<size_t>(std::atoll(argv[i + 1]));
        }
    }
    return 1'000'000;  // default
}

int main(int argc, char* argv[]) {
    size_t iterations = parse_iterations(argc, argv);

    std::cout << "================================================================\n";
    std::cout << " HFT Order Book Engine — Latency Histogram Benchmark\n";
    std::cout << "================================================================\n\n";

    setup_thread_affinity();

    std::cout << "\nCalibrating TSC frequency...\n";
    double tsc_freq = calibrate_tsc_frequency();
    std::cout << "TSC frequency: " << std::fixed << std::setprecision(3)
              << tsc_freq << " ticks/ns\n";

    std::cout << "Measuring rdtsc overhead...\n";
    g_rdtsc_overhead = measure_rdtsc_overhead();
    std::cout << "rdtsc overhead: " << g_rdtsc_overhead << " ticks ("
              << std::fixed << std::setprecision(1)
              << static_cast<double>(g_rdtsc_overhead) / tsc_freq << " ns)\n";
    std::cout << "Iterations per benchmark: " << iterations << "\n";

    // Latency benchmarks (smaller iteration count for multi-level to avoid
    // pool exhaustion — each iteration allocates 6 orders)
    size_t multi_iters = iterations < 150'000 ? iterations : 150'000;

    bench_add_order_no_match(iterations, tsc_freq);
    bench_cancel_order(iterations, tsc_freq);
    bench_match_single_level(iterations, tsc_freq);
    bench_match_multi_level(multi_iters, tsc_freq);
    bench_spsc_push_pop(iterations, tsc_freq);
    bench_throughput_sustained(iterations, tsc_freq);

    std::cout << "\n================================================================\n";
    std::cout << " Benchmark complete.\n";
    std::cout << "================================================================\n";

    return 0;
}
