#pragma once

/// @file price_level.h
/// @brief PriceLevel — intrusive FIFO queue of orders at a single price.
///
/// Orders within a price level are maintained in time priority (FIFO) via
/// an intrusive doubly-linked list. All operations are O(1):
///   - add_order:    append to tail
///   - remove_order: unlink from any position
///   - front:        return head

#include <cstdint>
#include <type_traits>

#include "core/order.h"
#include "core/types.h"

namespace hft {

struct PriceLevel {
    Price price;
    Quantity total_quantity;
    uint32_t order_count;
    Order* head;
    Order* tail;

    /// Append an order to the back of the FIFO queue (time priority).
    void add_order(Order* order) noexcept {
        order->next = nullptr;
        order->prev = tail;
        if (tail) {
            tail->next = order;
        } else {
            head = order;
        }
        tail = order;
        total_quantity += order->remaining_quantity();
        ++order_count;
    }

    /// Remove an order from any position in the queue. O(1) via prev/next.
    void remove_order(Order* order) noexcept {
        total_quantity -= order->remaining_quantity();
        --order_count;

        if (order->prev) {
            order->prev->next = order->next;
        } else {
            head = order->next;
        }

        if (order->next) {
            order->next->prev = order->prev;
        } else {
            tail = order->prev;
        }

        order->prev = nullptr;
        order->next = nullptr;
    }

    /// The first (oldest) order at this price level.
    [[nodiscard]] Order* front() const noexcept { return head; }

    /// True when no orders remain at this level.
    [[nodiscard]] bool empty() const noexcept { return head == nullptr; }
};

static_assert(std::is_trivially_copyable_v<PriceLevel>,
              "PriceLevel must be trivially copyable");
static_assert(std::is_standard_layout_v<PriceLevel>,
              "PriceLevel must be standard layout");

}  // namespace hft
