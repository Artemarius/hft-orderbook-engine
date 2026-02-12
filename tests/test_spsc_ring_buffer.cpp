/// @file test_spsc_ring_buffer.cpp
/// @brief Unit tests for SPSC ring buffer and transport message types.

#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "transport/message.h"
#include "transport/spsc_ring_buffer.h"

using namespace hft;

// ===========================================================================
// Type traits and layout verification
// ===========================================================================

TEST(SPSCTypeTraits, ElementMustBeTriviallyCopyable) {
    // The static_assert in the template enforces this at compile time.
    // This test documents the requirement.
    EXPECT_TRUE(std::is_trivially_copyable_v<int>);
    EXPECT_TRUE(std::is_trivially_copyable_v<OrderMessage>);
    EXPECT_TRUE(std::is_trivially_copyable_v<EventMessage>);
}

TEST(MessageLayout, OrderMessageSizeAndAlignment) {
    EXPECT_EQ(sizeof(OrderMessage), 128);
    EXPECT_EQ(alignof(OrderMessage), 64);
}

TEST(MessageLayout, EventMessageSizeAndAlignment) {
    EXPECT_EQ(sizeof(EventMessage), 64);
    EXPECT_EQ(alignof(EventMessage), 64);
}

TEST(MessageLayout, OrderEventDataSize) {
    EXPECT_EQ(sizeof(OrderEventData), 48);
}

TEST(MessageLayout, EventDataUnionSize) {
    EXPECT_EQ(sizeof(EventData), 48);
}

TEST(MessageLayout, TradeSize) {
    EXPECT_EQ(sizeof(Trade), 48);
}

TEST(SPSCLayout, CacheLinePadding) {
    // Verify head_ and tail_ are on separate cache lines.
    SPSCRingBuffer<uint64_t, 16> rb;

    // The buffer should be at least 2 cache lines for head + tail padding,
    // plus the buffer storage itself.
    EXPECT_GE(sizeof(rb), 64 * 2 + 16 * sizeof(uint64_t));
}

TEST(SPSCLayout, CapacityIsPowerOfTwo) {
    // Capacity 1, 2, 4, 8, etc. should compile.
    SPSCRingBuffer<int, 1> rb1;
    SPSCRingBuffer<int, 2> rb2;
    SPSCRingBuffer<int, 1024> rb1024;
    EXPECT_EQ(rb1.capacity(), 1);
    EXPECT_EQ(rb2.capacity(), 2);
    EXPECT_EQ(rb1024.capacity(), 1024);
}

// ===========================================================================
// Single-threaded correctness
// ===========================================================================

TEST(SPSCSingleThread, EmptyOnConstruction) {
    SPSCRingBuffer<int, 8> rb;
    EXPECT_TRUE(rb.empty());
    EXPECT_FALSE(rb.full());
    EXPECT_EQ(rb.size(), 0);
}

TEST(SPSCSingleThread, PopFromEmptyFails) {
    SPSCRingBuffer<int, 8> rb;
    int val = -1;
    EXPECT_FALSE(rb.try_pop(val));
    EXPECT_EQ(val, -1);  // unchanged
}

TEST(SPSCSingleThread, PushPopSingleItem) {
    SPSCRingBuffer<int, 8> rb;
    EXPECT_TRUE(rb.try_push(42));
    EXPECT_EQ(rb.size(), 1);

    int val = 0;
    EXPECT_TRUE(rb.try_pop(val));
    EXPECT_EQ(val, 42);
    EXPECT_TRUE(rb.empty());
}

TEST(SPSCSingleThread, FIFOOrdering) {
    SPSCRingBuffer<int, 8> rb;
    for (int i = 0; i < 8; ++i) {
        EXPECT_TRUE(rb.try_push(i));
    }

    for (int i = 0; i < 8; ++i) {
        int val = -1;
        EXPECT_TRUE(rb.try_pop(val));
        EXPECT_EQ(val, i);
    }
}

TEST(SPSCSingleThread, FullBufferRejectsPush) {
    SPSCRingBuffer<int, 4> rb;
    EXPECT_TRUE(rb.try_push(1));
    EXPECT_TRUE(rb.try_push(2));
    EXPECT_TRUE(rb.try_push(3));
    EXPECT_TRUE(rb.try_push(4));
    EXPECT_TRUE(rb.full());

    EXPECT_FALSE(rb.try_push(5));  // buffer full
    EXPECT_EQ(rb.size(), 4);
}

TEST(SPSCSingleThread, WrapAround) {
    SPSCRingBuffer<int, 4> rb;

    // Fill and drain multiple times to force wrap-around.
    for (int round = 0; round < 10; ++round) {
        for (int i = 0; i < 4; ++i) {
            EXPECT_TRUE(rb.try_push(round * 4 + i));
        }
        EXPECT_TRUE(rb.full());

        for (int i = 0; i < 4; ++i) {
            int val = -1;
            EXPECT_TRUE(rb.try_pop(val));
            EXPECT_EQ(val, round * 4 + i);
        }
        EXPECT_TRUE(rb.empty());
    }
}

TEST(SPSCSingleThread, InterleavedPushPop) {
    SPSCRingBuffer<int, 4> rb;

    EXPECT_TRUE(rb.try_push(1));
    EXPECT_TRUE(rb.try_push(2));

    int val = -1;
    EXPECT_TRUE(rb.try_pop(val));
    EXPECT_EQ(val, 1);

    EXPECT_TRUE(rb.try_push(3));
    EXPECT_TRUE(rb.try_push(4));

    EXPECT_TRUE(rb.try_pop(val));
    EXPECT_EQ(val, 2);
    EXPECT_TRUE(rb.try_pop(val));
    EXPECT_EQ(val, 3);
    EXPECT_TRUE(rb.try_pop(val));
    EXPECT_EQ(val, 4);

    EXPECT_TRUE(rb.empty());
}

TEST(SPSCSingleThread, CapacityOneEdgeCase) {
    SPSCRingBuffer<int, 1> rb;

    EXPECT_TRUE(rb.try_push(99));
    EXPECT_TRUE(rb.full());
    EXPECT_FALSE(rb.try_push(100));

    int val = -1;
    EXPECT_TRUE(rb.try_pop(val));
    EXPECT_EQ(val, 99);
    EXPECT_TRUE(rb.empty());

    // Again
    EXPECT_TRUE(rb.try_push(200));
    EXPECT_TRUE(rb.try_pop(val));
    EXPECT_EQ(val, 200);
}

// ===========================================================================
// Message round-trip tests
// ===========================================================================

TEST(SPSCMessageRoundTrip, OrderMessage) {
    SPSCRingBuffer<OrderMessage, 16> rb;

    OrderMessage msg{};
    msg.type = MessageType::Add;
    msg.order.order_id = 42;
    msg.order.participant_id = 7;
    msg.order.side = Side::Buy;
    msg.order.type = OrderType::Limit;
    msg.order.time_in_force = TimeInForce::GTC;
    msg.order.status = OrderStatus::New;
    msg.order.price = 50000 * PRICE_SCALE;
    msg.order.quantity = 100;
    msg.order.visible_quantity = 100;
    msg.order.iceberg_slice_qty = 0;
    msg.order.filled_quantity = 0;
    msg.order.timestamp = 1234567890;
    msg.order.next = nullptr;
    msg.order.prev = nullptr;

    EXPECT_TRUE(rb.try_push(msg));

    OrderMessage out{};
    EXPECT_TRUE(rb.try_pop(out));

    EXPECT_EQ(out.type, MessageType::Add);
    EXPECT_EQ(out.order.order_id, 42);
    EXPECT_EQ(out.order.participant_id, 7);
    EXPECT_EQ(out.order.side, Side::Buy);
    EXPECT_EQ(out.order.type, OrderType::Limit);
    EXPECT_EQ(out.order.price, 50000 * PRICE_SCALE);
    EXPECT_EQ(out.order.quantity, 100);
    EXPECT_EQ(out.order.timestamp, 1234567890);
}

TEST(SPSCMessageRoundTrip, EventMessageTrade) {
    SPSCRingBuffer<EventMessage, 16> rb;

    EventMessage msg{};
    msg.type = EventType::Trade;
    msg.sequence_num = 1;
    msg.data.trade.trade_id = 100;
    msg.data.trade.buy_order_id = 10;
    msg.data.trade.sell_order_id = 20;
    msg.data.trade.price = 42000 * PRICE_SCALE;
    msg.data.trade.quantity = 50;
    msg.data.trade.timestamp = 9999;

    EXPECT_TRUE(rb.try_push(msg));

    EventMessage out{};
    EXPECT_TRUE(rb.try_pop(out));

    EXPECT_EQ(out.type, EventType::Trade);
    EXPECT_EQ(out.sequence_num, 1);
    EXPECT_EQ(out.data.trade.trade_id, 100);
    EXPECT_EQ(out.data.trade.buy_order_id, 10);
    EXPECT_EQ(out.data.trade.sell_order_id, 20);
    EXPECT_EQ(out.data.trade.price, 42000 * PRICE_SCALE);
    EXPECT_EQ(out.data.trade.quantity, 50);
    EXPECT_EQ(out.data.trade.timestamp, 9999);
}

TEST(SPSCMessageRoundTrip, EventMessageOrderEvent) {
    SPSCRingBuffer<EventMessage, 16> rb;

    EventMessage msg{};
    msg.type = EventType::OrderAccepted;
    msg.sequence_num = 2;
    msg.data.order_event.order_id = 55;
    msg.data.order_event.status = OrderStatus::Accepted;
    msg.data.order_event.filled_quantity = 0;
    msg.data.order_event.remaining_quantity = 200;
    msg.data.order_event.price = 30000 * PRICE_SCALE;
    msg.data.order_event.timestamp = 5555;

    EXPECT_TRUE(rb.try_push(msg));

    EventMessage out{};
    EXPECT_TRUE(rb.try_pop(out));

    EXPECT_EQ(out.type, EventType::OrderAccepted);
    EXPECT_EQ(out.sequence_num, 2);
    EXPECT_EQ(out.data.order_event.order_id, 55);
    EXPECT_EQ(out.data.order_event.status, OrderStatus::Accepted);
    EXPECT_EQ(out.data.order_event.filled_quantity, 0);
    EXPECT_EQ(out.data.order_event.remaining_quantity, 200);
    EXPECT_EQ(out.data.order_event.price, 30000 * PRICE_SCALE);
    EXPECT_EQ(out.data.order_event.timestamp, 5555);
}

// ===========================================================================
// Multi-threaded correctness
// ===========================================================================

TEST(SPSCMultiThread, ProducerConsumer1MItems) {
    constexpr size_t kCount = 1'000'000;
    SPSCRingBuffer<uint64_t, 1024> rb;

    std::atomic<bool> done{false};
    std::vector<uint64_t> received;
    received.reserve(kCount);

    // Consumer thread
    std::thread consumer([&] {
        uint64_t val = 0;
        size_t popped = 0;
        while (popped < kCount) {
            if (rb.try_pop(val)) {
                received.push_back(val);
                ++popped;
            }
        }
        done.store(true, std::memory_order_release);
    });

    // Producer thread (this thread)
    for (uint64_t i = 0; i < kCount; ++i) {
        while (!rb.try_push(i)) {
            // spin — buffer full
        }
    }

    consumer.join();

    // Verify all items arrived in order
    ASSERT_EQ(received.size(), kCount);
    for (uint64_t i = 0; i < kCount; ++i) {
        EXPECT_EQ(received[i], i) << "Mismatch at index " << i;
    }
}

TEST(SPSCMultiThread, EventMessageStress) {
    constexpr size_t kCount = 500'000;
    SPSCRingBuffer<EventMessage, 1024> rb;

    std::vector<EventMessage> received;
    received.reserve(kCount);

    std::thread consumer([&] {
        EventMessage msg{};
        size_t popped = 0;
        while (popped < kCount) {
            if (rb.try_pop(msg)) {
                received.push_back(msg);
                ++popped;
            }
        }
    });

    for (uint64_t i = 0; i < kCount; ++i) {
        EventMessage msg{};
        msg.type = EventType::Trade;
        msg.sequence_num = i;
        msg.data.trade.trade_id = i;
        msg.data.trade.price = static_cast<Price>(i * 100);
        msg.data.trade.quantity = i + 1;

        while (!rb.try_push(msg)) {
            // spin
        }
    }

    consumer.join();

    ASSERT_EQ(received.size(), kCount);
    for (uint64_t i = 0; i < kCount; ++i) {
        EXPECT_EQ(received[i].sequence_num, i);
        EXPECT_EQ(received[i].data.trade.trade_id, i);
        EXPECT_EQ(received[i].data.trade.price, static_cast<Price>(i * 100));
        EXPECT_EQ(received[i].data.trade.quantity, i + 1);
    }
}

TEST(SPSCMultiThread, OrderMessageStress) {
    constexpr size_t kCount = 200'000;
    SPSCRingBuffer<OrderMessage, 512> rb;

    std::vector<OrderMessage> received;
    received.reserve(kCount);

    std::thread consumer([&] {
        OrderMessage msg{};
        size_t popped = 0;
        while (popped < kCount) {
            if (rb.try_pop(msg)) {
                received.push_back(msg);
                ++popped;
            }
        }
    });

    for (uint64_t i = 0; i < kCount; ++i) {
        OrderMessage msg{};
        msg.type = MessageType::Add;
        msg.order.order_id = i;
        msg.order.price = static_cast<Price>(i * 10);
        msg.order.quantity = i + 1;
        msg.order.side = (i % 2 == 0) ? Side::Buy : Side::Sell;

        while (!rb.try_push(msg)) {
            // spin
        }
    }

    consumer.join();

    ASSERT_EQ(received.size(), kCount);
    for (uint64_t i = 0; i < kCount; ++i) {
        EXPECT_EQ(received[i].order.order_id, i);
        EXPECT_EQ(received[i].order.price, static_cast<Price>(i * 10));
        EXPECT_EQ(received[i].order.quantity, i + 1);
    }
}

TEST(SPSCMultiThread, AsymmetricSpeed) {
    // Producer is faster than consumer (consumer does extra work).
    constexpr size_t kCount = 100'000;
    SPSCRingBuffer<uint64_t, 64> rb;  // small buffer to force backpressure

    std::atomic<uint64_t> sum{0};

    std::thread consumer([&] {
        uint64_t val = 0;
        uint64_t local_sum = 0;
        size_t popped = 0;
        while (popped < kCount) {
            if (rb.try_pop(val)) {
                local_sum += val;
                ++popped;
            }
        }
        sum.store(local_sum, std::memory_order_release);
    });

    for (uint64_t i = 0; i < kCount; ++i) {
        while (!rb.try_push(i)) {
            // spin — backpressure
        }
    }

    consumer.join();

    // sum of 0..N-1 = N*(N-1)/2
    uint64_t expected = kCount * (kCount - 1) / 2;
    EXPECT_EQ(sum.load(std::memory_order_acquire), expected);
}
