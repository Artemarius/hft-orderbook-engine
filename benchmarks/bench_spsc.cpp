/// @file bench_spsc.cpp
/// @brief Throughput and latency benchmarks for SPSC ring buffer.

#include <atomic>
#include <cstdint>
#include <thread>

#include <benchmark/benchmark.h>

#include "transport/message.h"
#include "transport/spsc_ring_buffer.h"

using namespace hft;

// ---------------------------------------------------------------------------
// Single-threaded push+pop latency (measures ring buffer overhead only)
// ---------------------------------------------------------------------------

static void BM_SPSCPushPop(benchmark::State& state) {
    SPSCRingBuffer<uint64_t, 1024> rb;
    uint64_t val = 0;

    for (auto _ : state) {
        (void)rb.try_push(val);
        (void)rb.try_pop(val);
        benchmark::DoNotOptimize(val);
    }
}
BENCHMARK(BM_SPSCPushPop);

static void BM_SPSCPushPop_EventMessage(benchmark::State& state) {
    SPSCRingBuffer<EventMessage, 1024> rb;
    EventMessage msg{};
    msg.type = EventType::Trade;
    msg.sequence_num = 0;
    msg.data.trade.trade_id = 1;
    msg.data.trade.price = 50000;
    msg.data.trade.quantity = 100;

    EventMessage out{};

    for (auto _ : state) {
        (void)rb.try_push(msg);
        (void)rb.try_pop(out);
        benchmark::DoNotOptimize(out);
    }
}
BENCHMARK(BM_SPSCPushPop_EventMessage);

static void BM_SPSCPushPop_OrderMessage(benchmark::State& state) {
    SPSCRingBuffer<OrderMessage, 1024> rb;
    OrderMessage msg{};
    msg.type = MessageType::Add;
    msg.order.order_id = 1;
    msg.order.price = 50000;
    msg.order.quantity = 100;

    OrderMessage out{};

    for (auto _ : state) {
        (void)rb.try_push(msg);
        (void)rb.try_pop(out);
        benchmark::DoNotOptimize(out);
    }
}
BENCHMARK(BM_SPSCPushPop_OrderMessage);

// ---------------------------------------------------------------------------
// Two-thread throughput (producer/consumer on separate threads)
// ---------------------------------------------------------------------------

static void BM_SPSCThroughput(benchmark::State& state) {
    const size_t count = static_cast<size_t>(state.range(0));

    for (auto _ : state) {
        SPSCRingBuffer<uint64_t, 8192> rb;
        std::atomic<bool> producer_done{false};

        std::thread consumer([&] {
            uint64_t val = 0;
            size_t popped = 0;
            while (popped < count) {
                if (rb.try_pop(val)) {
                    benchmark::DoNotOptimize(val);
                    ++popped;
                }
            }
        });

        for (uint64_t i = 0; i < count; ++i) {
            while (!rb.try_push(i)) {
                // spin
            }
        }

        consumer.join();
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                            state.range(0));
}
BENCHMARK(BM_SPSCThroughput)->Arg(1'000'000)->Arg(10'000'000);

// ---------------------------------------------------------------------------
// Two-thread throughput with EventMessage payload
// ---------------------------------------------------------------------------

static void BM_SPSCThroughput_EventMessage(benchmark::State& state) {
    const size_t count = static_cast<size_t>(state.range(0));

    for (auto _ : state) {
        SPSCRingBuffer<EventMessage, 8192> rb;

        std::thread consumer([&] {
            EventMessage msg{};
            size_t popped = 0;
            while (popped < count) {
                if (rb.try_pop(msg)) {
                    benchmark::DoNotOptimize(msg);
                    ++popped;
                }
            }
        });

        EventMessage msg{};
        msg.type = EventType::Trade;
        for (uint64_t i = 0; i < count; ++i) {
            msg.sequence_num = i;
            while (!rb.try_push(msg)) {
                // spin
            }
        }

        consumer.join();
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                            state.range(0));
}
BENCHMARK(BM_SPSCThroughput_EventMessage)->Arg(1'000'000);
