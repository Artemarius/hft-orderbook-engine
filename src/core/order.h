#pragma once

/// @file order.h
/// @brief Order struct — fixed-size POD for the matching engine hot path.
///
/// Order is an intrusive doubly-linked list node for O(1) insertion and
/// removal within a PriceLevel's FIFO queue. Trivially copyable,
/// cache-friendly, and free of heap allocation.

#include <cstddef>
#include <type_traits>

#include "core/types.h"

namespace hft {

struct Order {
    OrderId order_id;
    ParticipantId participant_id;
    InstrumentId instrument_id;
    Side side;
    OrderType type;
    TimeInForce time_in_force;
    OrderStatus status;
    Price price;
    Quantity quantity;
    Quantity visible_quantity;   // Iceberg: displayed quantity
    Quantity iceberg_slice_qty;  // Iceberg: original display slice size (for replenishment)
    Quantity filled_quantity;
    Timestamp timestamp;
    Order* next;                // Intrusive list: next order in price level
    Order* prev;                // Intrusive list: prev order in price level

    /// Remaining unfilled quantity.
    [[nodiscard]] Quantity remaining_quantity() const noexcept {
        return quantity - filled_quantity;
    }

    /// Remaining visible quantity (for iceberg orders).
    [[nodiscard]] Quantity remaining_visible() const noexcept {
        return visible_quantity - filled_quantity;
    }
};

// Hot-path contract: Order must be trivially copyable and fit in 2 cache lines.
static_assert(std::is_trivially_copyable_v<Order>,
              "Order must be trivially copyable for hot-path use");
static_assert(std::is_standard_layout_v<Order>,
              "Order must be standard layout for predictable memory layout");
static_assert(sizeof(Order) <= 128,
              "Order must fit in 2 cache lines (128 bytes)");
static_assert(alignof(Order) <= 64,
              "Order alignment must not exceed cache line size");

}  // namespace hft
