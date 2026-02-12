#pragma once

/// @file order_gateway.h
/// @brief Order ingestion gateway — validates, submits to matching engine,
///        decomposes results into EventMessages for the SPSC event buffer.
///
/// Cold-path library. Sits on Thread 1 (the matching thread) but uses
/// std::function-free, allocation-free logic. The event buffer pointer
/// is nullable for testing without a publisher.

#include <cstdint>

#include "core/order.h"
#include "core/types.h"
#include "matching/match_result.h"
#include "matching/matching_engine.h"
#include "orderbook/memory_pool.h"
#include "transport/event_buffer.h"
#include "transport/message.h"

namespace hft {

/// Reason the gateway rejected an order before it reached the engine.
enum class GatewayRejectReason : uint8_t {
    None,
    InvalidPrice,
    InvalidQuantity,
    PoolExhausted,
    OrderNotFound
};

/// Lightweight result returned to the caller of process_order().
/// Detailed per-trade events are published to the EventBuffer.
struct GatewayResult {
    bool accepted;
    GatewayRejectReason reject_reason;
    MatchStatus match_status;
    uint32_t trade_count;
    Quantity filled_quantity;
    Quantity remaining_quantity;
};

class OrderGateway {
public:
    /// @param engine        Matching engine to forward validated orders to.
    /// @param pool          Memory pool for Order allocation.
    /// @param event_buffer  Nullable — if nullptr, no events are published.
    /// @param instrument_id Instrument this gateway serves (default: 0).
    OrderGateway(MatchingEngine& engine, MemoryPool<Order>& pool,
                 EventBuffer* event_buffer,
                 InstrumentId instrument_id = DEFAULT_INSTRUMENT_ID) noexcept;

    OrderGateway(const OrderGateway&) = delete;
    OrderGateway& operator=(const OrderGateway&) = delete;

    /// Validate an inbound order, submit to the matching engine, and
    /// publish decomposed EventMessages to the event buffer.
    [[nodiscard]] GatewayResult process_order(const OrderMessage& msg) noexcept;

    /// Cancel an order by ID. Publishes OrderCancelled if successful.
    [[nodiscard]] bool process_cancel(OrderId order_id) noexcept;

    /// Modify a resting order's price and/or quantity. Publishes
    /// OrderModified, plus Trade/OrderFilled events if the new price crosses.
    [[nodiscard]] GatewayResult process_modify(const OrderMessage& msg) noexcept;

    [[nodiscard]] uint64_t orders_processed() const noexcept { return orders_processed_; }
    [[nodiscard]] uint64_t orders_rejected() const noexcept { return orders_rejected_; }
    [[nodiscard]] uint64_t sequence_number() const noexcept { return sequence_num_; }
    [[nodiscard]] uint64_t backpressure_count() const noexcept { return backpressure_count_; }

private:
    /// Spin-wait push to the event buffer.
    void publish_event(const EventMessage& event) noexcept;

    /// Publish an OrderRejected event for a gateway-level rejection.
    void publish_rejection(const Order& src) noexcept;

    /// Decompose a MatchResult into Trade + order status EventMessages.
    void decompose_and_publish(const MatchResult& result,
                               const Order& order_copy) noexcept;

    [[nodiscard]] uint64_t next_sequence_num() noexcept;

    MatchingEngine& engine_;
    MemoryPool<Order>& pool_;
    EventBuffer* event_buffer_;
    InstrumentId instrument_id_;
    uint64_t sequence_num_;
    uint64_t orders_processed_;
    uint64_t orders_rejected_;
    uint64_t backpressure_count_;
};

}  // namespace hft
