#include "gateway/order_gateway.h"

#include <cstring>

namespace hft {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

OrderGateway::OrderGateway(MatchingEngine& engine, MemoryPool<Order>& pool,
                           EventBuffer* event_buffer) noexcept
    : engine_(engine),
      pool_(pool),
      event_buffer_(event_buffer),
      sequence_num_(0),
      orders_processed_(0),
      orders_rejected_(0),
      backpressure_count_(0) {}

// ---------------------------------------------------------------------------
// Order submission
// ---------------------------------------------------------------------------

GatewayResult OrderGateway::process_order(const OrderMessage& msg) noexcept {
    GatewayResult result{};
    result.accepted = false;
    result.reject_reason = GatewayRejectReason::None;
    result.match_status = MatchStatus::Rejected;
    result.trade_count = 0;
    result.filled_quantity = 0;
    result.remaining_quantity = 0;

    const Order& src = msg.order;

    // --- Gateway-level validation ---

    // Quantity must be positive
    if (src.quantity == 0) {
        result.reject_reason = GatewayRejectReason::InvalidQuantity;
        ++orders_rejected_;
        publish_rejection(src);
        return result;
    }

    // Non-Market orders must have a positive price
    if (src.type != OrderType::Market && src.price <= 0) {
        result.reject_reason = GatewayRejectReason::InvalidPrice;
        ++orders_rejected_;
        publish_rejection(src);
        return result;
    }

    // Allocate from pool
    Order* order = pool_.allocate();
    if (!order) {
        result.reject_reason = GatewayRejectReason::PoolExhausted;
        ++orders_rejected_;
        publish_rejection(src);
        return result;
    }

    // --- Initialize order from message ---

    *order = src;
    order->status = OrderStatus::New;
    order->filled_quantity = 0;
    order->next = nullptr;
    order->prev = nullptr;

    // Save a copy — the matching engine may deallocate the order
    Order order_copy;
    std::memcpy(&order_copy, order, sizeof(Order));

    // --- Submit to matching engine ---

    MatchResult match_result = engine_.submit_order(order);
    // `order` may be deallocated at this point — do not dereference.

    // --- Decompose and publish events ---

    decompose_and_publish(match_result, order_copy);

    // --- Build lightweight result ---

    result.accepted = true;
    result.match_status = match_result.status;
    result.trade_count = match_result.trade_count;
    result.filled_quantity = match_result.filled_quantity;
    result.remaining_quantity = match_result.remaining_quantity;
    ++orders_processed_;

    return result;
}

// ---------------------------------------------------------------------------
// Modify
// ---------------------------------------------------------------------------

GatewayResult OrderGateway::process_modify(const OrderMessage& msg) noexcept {
    GatewayResult result{};
    result.accepted = false;
    result.reject_reason = GatewayRejectReason::None;
    result.match_status = MatchStatus::Rejected;
    result.trade_count = 0;
    result.filled_quantity = 0;
    result.remaining_quantity = 0;

    const Order& src = msg.order;

    // Gateway-level validation
    if (src.quantity == 0) {
        result.reject_reason = GatewayRejectReason::InvalidQuantity;
        ++orders_rejected_;
        publish_rejection(src);
        return result;
    }

    if (src.price <= 0) {
        result.reject_reason = GatewayRejectReason::InvalidPrice;
        ++orders_rejected_;
        publish_rejection(src);
        return result;
    }

    // Submit to matching engine
    MatchResult match_result = engine_.modify_order(
        src.order_id, src.price, src.quantity, src.timestamp);

    if (match_result.status == MatchStatus::Rejected) {
        result.reject_reason = GatewayRejectReason::OrderNotFound;
        ++orders_rejected_;
        publish_rejection(src);
        return result;
    }

    // Build an order_copy for event decomposition using the new values
    Order order_copy{};
    order_copy.order_id = src.order_id;
    order_copy.price = src.price;
    order_copy.timestamp = src.timestamp;

    decompose_and_publish(match_result, order_copy);

    result.accepted = true;
    result.match_status = match_result.status;
    result.trade_count = match_result.trade_count;
    result.filled_quantity = match_result.filled_quantity;
    result.remaining_quantity = match_result.remaining_quantity;
    ++orders_processed_;

    return result;
}

// ---------------------------------------------------------------------------
// Cancel
// ---------------------------------------------------------------------------

bool OrderGateway::process_cancel(OrderId order_id) noexcept {
    bool success = engine_.cancel_order(order_id);

    if (success && event_buffer_) {
        EventMessage event{};
        event.type = EventType::OrderCancelled;
        event.sequence_num = next_sequence_num();
        event.data.order_event.order_id = order_id;
        event.data.order_event.status = OrderStatus::Cancelled;
        event.data.order_event.filled_quantity = 0;
        event.data.order_event.remaining_quantity = 0;
        event.data.order_event.price = 0;
        event.data.order_event.timestamp = 0;
        publish_event(event);
    }

    return success;
}

// ---------------------------------------------------------------------------
// Event decomposition
// ---------------------------------------------------------------------------

void OrderGateway::decompose_and_publish(const MatchResult& result,
                                          const Order& order_copy) noexcept {
    if (!event_buffer_) return;

    // Trades published first (price-time priority audit trail)
    for (uint32_t i = 0; i < result.trade_count; ++i) {
        EventMessage event{};
        event.type = EventType::Trade;
        event.sequence_num = next_sequence_num();
        event.data.trade = result.trades[i];
        publish_event(event);
    }

    // Then terminal order status
    EventMessage event{};
    event.sequence_num = next_sequence_num();
    event.data.order_event.order_id = order_copy.order_id;
    event.data.order_event.filled_quantity = result.filled_quantity;
    event.data.order_event.remaining_quantity = result.remaining_quantity;
    event.data.order_event.price = order_copy.price;
    event.data.order_event.timestamp = order_copy.timestamp;

    switch (result.status) {
        case MatchStatus::Filled:
            event.type = EventType::OrderFilled;
            event.data.order_event.status = OrderStatus::Filled;
            break;
        case MatchStatus::PartialFill:
            event.type = EventType::OrderPartialFill;
            event.data.order_event.status = OrderStatus::PartialFill;
            break;
        case MatchStatus::Resting:
            event.type = EventType::OrderAccepted;
            event.data.order_event.status = OrderStatus::Accepted;
            break;
        case MatchStatus::Cancelled:
            event.type = EventType::OrderCancelled;
            event.data.order_event.status = OrderStatus::Cancelled;
            break;
        case MatchStatus::Rejected:
            event.type = EventType::OrderRejected;
            event.data.order_event.status = OrderStatus::Rejected;
            break;
        case MatchStatus::SelfTradePrevented:
            event.type = EventType::OrderCancelled;
            event.data.order_event.status = OrderStatus::Cancelled;
            break;
        case MatchStatus::Modified:
            event.type = EventType::OrderModified;
            event.data.order_event.status = OrderStatus::Accepted;
            break;
    }

    publish_event(event);
}

// ---------------------------------------------------------------------------
// Publishing helpers
// ---------------------------------------------------------------------------

void OrderGateway::publish_rejection(const Order& src) noexcept {
    if (!event_buffer_) return;

    EventMessage event{};
    event.type = EventType::OrderRejected;
    event.sequence_num = next_sequence_num();
    event.data.order_event.order_id = src.order_id;
    event.data.order_event.status = OrderStatus::Rejected;
    event.data.order_event.filled_quantity = 0;
    event.data.order_event.remaining_quantity = src.quantity;
    event.data.order_event.price = src.price;
    event.data.order_event.timestamp = src.timestamp;
    publish_event(event);
}

void OrderGateway::publish_event(const EventMessage& event) noexcept {
    if (!event_buffer_) return;

    while (!event_buffer_->try_push(event)) {
        ++backpressure_count_;
        // Spin-wait — backpressure from slow consumer
    }
}

uint64_t OrderGateway::next_sequence_num() noexcept {
    return ++sequence_num_;
}

}  // namespace hft
