#pragma once

/// @file message.h
/// @brief Fixed-size POD message types for inter-thread transport.
///
/// Messages are designed for zero-copy transfer through SPSC ring buffers.
/// Each message type is cache-line aligned and trivially copyable.
///
/// OrderMessage (128 bytes, 2 cache lines): inbound order commands.
/// EventMessage (64 bytes, 1 cache line): outbound matching events.

#include <cstdint>
#include <type_traits>

#include "core/order.h"
#include "core/trade.h"
#include "core/types.h"

namespace hft {

// ---------------------------------------------------------------------------
// OrderMessage — inbound order commands (gateway -> matching engine)
// ---------------------------------------------------------------------------

/// Type of order command entering the matching engine.
enum class MessageType : uint8_t {
    Add,
    Cancel,
    Modify
};

/// Fixed-size inbound order message. Carries a full Order struct so the
/// matching engine can operate on it directly without extra lookups.
struct alignas(64) OrderMessage {
    MessageType type;       // 1 byte
    uint8_t pad_[7];        // 7 bytes padding for 8-byte alignment
    Order order;            // 80 bytes
    // implicit trailing padding to 128 bytes (alignas(64), next multiple)
};

static_assert(std::is_trivially_copyable_v<OrderMessage>,
              "OrderMessage must be trivially copyable");
static_assert(std::is_standard_layout_v<OrderMessage>,
              "OrderMessage must be standard layout");
static_assert(sizeof(OrderMessage) == 128,
              "OrderMessage must be exactly 128 bytes (2 cache lines)");
static_assert(alignof(OrderMessage) == 64,
              "OrderMessage must be cache-line aligned");

// ---------------------------------------------------------------------------
// EventMessage — outbound matching events (matching engine -> consumers)
// ---------------------------------------------------------------------------

/// Type of event produced by the matching engine.
enum class EventType : uint8_t {
    Trade,              // A trade was executed
    OrderAccepted,      // Order accepted and resting on book
    OrderCancelled,     // Order was cancelled
    OrderRejected,      // Order was rejected (e.g. FOK not feasible)
    OrderFilled,        // Order fully filled (terminal)
    OrderPartialFill,   // Order partially filled
    OrderModified       // Order modified (price/quantity amended)
};

/// Order event data — status update for a single order.
struct OrderEventData {
    OrderId order_id;
    OrderStatus status;
    uint8_t pad_[7];
    Quantity filled_quantity;
    Quantity remaining_quantity;
    Price price;
    Timestamp timestamp;
};

static_assert(sizeof(OrderEventData) == 48,
              "OrderEventData must be exactly 48 bytes");
static_assert(std::is_trivially_copyable_v<OrderEventData>,
              "OrderEventData must be trivially copyable");

/// Discriminated union of event payloads.
union EventData {
    Trade trade;               // 48 bytes
    OrderEventData order_event; // 48 bytes
};

static_assert(sizeof(EventData) == 48,
              "EventData union must be exactly 48 bytes");
static_assert(std::is_trivially_copyable_v<EventData>,
              "EventData must be trivially copyable");

/// Fixed-size outbound event message. One event per trade or order status
/// change — decomposed from MatchResult for efficient ring buffer transport.
struct alignas(64) EventMessage {
    EventType type;          // 1 byte
    uint8_t pad_[7];         // 7 bytes padding for 8-byte alignment
    uint64_t sequence_num;   // 8 bytes — monotonically increasing sequence
    EventData data;          // 48 bytes
};

static_assert(std::is_trivially_copyable_v<EventMessage>,
              "EventMessage must be trivially copyable");
static_assert(std::is_standard_layout_v<EventMessage>,
              "EventMessage must be standard layout");
static_assert(sizeof(EventMessage) == 64,
              "EventMessage must be exactly 64 bytes (1 cache line)");
static_assert(alignof(EventMessage) == 64,
              "EventMessage must be cache-line aligned");

}  // namespace hft
