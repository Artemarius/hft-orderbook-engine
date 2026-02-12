#pragma once

/// @file order_book.h
/// @brief Per-instrument limit order book with O(1) best bid/ask.
///
/// Uses flat arrays indexed by price tick (Option A from ROADMAP.md) for
/// cache-friendly, O(1) access to any price level. Two separate arrays
/// for bid and ask sides. Order lookup by ID via FlatOrderMap.
///
/// Price range and tick size are fixed at construction. All memory is
/// pre-allocated — zero heap allocation after startup.

#include <cstddef>

#include "core/order.h"
#include "core/price_level.h"
#include "core/types.h"
#include "orderbook/flat_order_map.h"

namespace hft {

/// Snapshot of a single price level for depth queries.
struct DepthEntry {
    Price price;
    Quantity quantity;
    uint32_t order_count;
};

struct AddResult {
    bool success;
};

struct CancelResult {
    bool success;
    Order* order;  // The cancelled order (caller can return to pool)
};

class OrderBook {
public:
    /// @param min_price  Lowest supported price (fixed-point).
    /// @param max_price  Highest supported price (fixed-point).
    /// @param tick_size  Minimum price increment (fixed-point).
    /// @param max_orders Maximum number of live orders (sizes the hash map).
    OrderBook(Price min_price, Price max_price, Price tick_size,
              size_t max_orders);
    ~OrderBook();

    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    /// Place an order on the book. Does not perform matching (Phase 3).
    /// Returns success=false if price is out of range, not tick-aligned,
    /// or order ID is duplicate.
    AddResult add_order(Order* order) noexcept;

    /// Cancel an order by ID. Returns the cancelled order pointer so the
    /// caller can return it to the memory pool.
    CancelResult cancel_order(OrderId id) noexcept;

    /// Remove a specific order from its price level and the order map.
    /// Used by the matching engine after fills.
    void remove_order(Order* order) noexcept;

    /// Best bid level (highest price with buy orders), or nullptr.
    [[nodiscard]] const PriceLevel* best_bid() const noexcept;

    /// Best ask level (lowest price with sell orders), or nullptr.
    [[nodiscard]] const PriceLevel* best_ask() const noexcept;

    /// Mutable access to best levels (for matching engine).
    [[nodiscard]] PriceLevel* best_bid_level() noexcept;
    [[nodiscard]] PriceLevel* best_ask_level() noexcept;

    /// Spread = best_ask - best_bid. Returns -1 if either side is empty.
    [[nodiscard]] Price spread() const noexcept;

    /// Mid price = (best_bid + best_ask) / 2. Returns 0 if either side empty.
    [[nodiscard]] Price mid_price() const noexcept;

    /// Look up an order by ID. Returns nullptr if not found.
    [[nodiscard]] Order* find_order(OrderId id) const noexcept;

    [[nodiscard]] size_t order_count() const noexcept { return order_count_; }
    [[nodiscard]] bool empty() const noexcept { return order_count_ == 0; }

    [[nodiscard]] Price min_price() const noexcept { return min_price_; }
    [[nodiscard]] Price max_price() const noexcept { return max_price_; }
    [[nodiscard]] Price tick_size() const noexcept { return tick_size_; }
    [[nodiscard]] size_t num_levels() const noexcept { return num_levels_; }

    /// Check if a price is within range and tick-aligned.
    [[nodiscard]] bool is_valid_price(Price price) const noexcept;

    /// Total quantity available on `side` at prices that would cross `limit_price`.
    /// Used by FOK feasibility check. Read-only walk of the flat price array.
    [[nodiscard]] Quantity available_quantity(Side side, Price limit_price) const noexcept;

    /// Fill `out` with up to `max_levels` non-empty bid levels starting from best bid.
    /// Returns number of levels filled. Bids are ordered best (highest) to worst.
    [[nodiscard]] size_t get_bid_depth(DepthEntry* out, size_t max_levels) const noexcept;

    /// Fill `out` with up to `max_levels` non-empty ask levels starting from best ask.
    /// Returns number of levels filled. Asks are ordered best (lowest) to worst.
    [[nodiscard]] size_t get_ask_depth(DepthEntry* out, size_t max_levels) const noexcept;

private:
    static constexpr size_t INVALID_INDEX = SIZE_MAX;

    [[nodiscard]] size_t price_to_index(Price price) const noexcept;
    [[nodiscard]] Price index_to_price(size_t index) const noexcept;

    void update_best_bid_after_remove(size_t emptied_idx) noexcept;
    void update_best_ask_after_remove(size_t emptied_idx) noexcept;

    PriceLevel* bid_levels_;
    PriceLevel* ask_levels_;
    size_t num_levels_;

    Price min_price_;
    Price max_price_;
    Price tick_size_;

    size_t best_bid_idx_;
    size_t best_ask_idx_;

    FlatOrderMap order_map_;
    size_t order_count_;
};

}  // namespace hft
