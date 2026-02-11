#pragma once

/// @file matching_engine.h
/// @brief Price-time priority matching engine for a single instrument.
///
/// Performs matching on incoming orders against the order book, generating
/// trades. Supports Limit, Market, IOC, FOK, GTC, and Iceberg order types.
/// Self-trade prevention is configurable at construction.
///
/// Zero heap allocation on the hot path — all trades are returned in a
/// fixed-size MatchResult struct on the stack.

#include <cstdint>

#include "core/order.h"
#include "core/price_level.h"
#include "core/types.h"
#include "matching/match_result.h"
#include "orderbook/memory_pool.h"
#include "orderbook/order_book.h"

namespace hft {

class MatchingEngine {
public:
    /// @param book   Order book to match against (caller owns lifetime).
    /// @param pool   Memory pool for Order allocation/deallocation.
    /// @param stp    Self-trade prevention mode.
    MatchingEngine(OrderBook& book, MemoryPool<Order>& pool,
                   SelfTradePreventionMode stp = SelfTradePreventionMode::CancelNewest) noexcept;

    MatchingEngine(const MatchingEngine&) = delete;
    MatchingEngine& operator=(const MatchingEngine&) = delete;

    /// Submit an order for matching. The order must be allocated from `pool_`.
    /// Fully filled orders are deallocated. Remaining GTC/Limit orders rest
    /// on the book. IOC/Market remainders are cancelled and deallocated.
    [[nodiscard]] MatchResult submit_order(Order* order) noexcept;

    /// Cancel an order by ID. Removes from book and deallocates from pool.
    [[nodiscard]] bool cancel_order(OrderId id) noexcept;

    [[nodiscard]] SelfTradePreventionMode stp_mode() const noexcept { return stp_mode_; }
    [[nodiscard]] uint64_t total_trade_count() const noexcept { return trade_id_counter_; }

private:
    /// Core matching loop — walks opposite side levels, fills, generates trades.
    void match_order(Order* order, MatchResult& result) noexcept;

    /// Check if a FOK order can be fully filled before attempting to match.
    [[nodiscard]] bool check_fok_feasibility(const Order* order) const noexcept;

    /// Execute a fill between aggressive and resting orders.
    void execute_fill(Order* aggressive, Order* resting,
                      Quantity fill_qty, PriceLevel* level,
                      MatchResult& result) noexcept;

    /// Check if two orders would be a self-trade.
    [[nodiscard]] static bool check_self_trade(const Order* aggressive,
                                               const Order* resting) noexcept;

    /// Handle a self-trade according to stp_mode_. Returns true if the
    /// aggressive order should stop matching (was cancelled).
    [[nodiscard]] bool handle_self_trade(Order* aggressive, Order* resting,
                                         PriceLevel* level,
                                         MatchResult& result) noexcept;

    /// Replenish an iceberg order's visible quantity after it's been fully matched.
    static void replenish_iceberg(Order* order) noexcept;

    /// Check if an incoming order's price crosses a price level.
    [[nodiscard]] static bool price_crosses(const Order* order,
                                            const PriceLevel* level) noexcept;

    /// Generate the next monotonically increasing trade ID.
    [[nodiscard]] uint64_t next_trade_id() noexcept;

    OrderBook& book_;
    MemoryPool<Order>& pool_;
    SelfTradePreventionMode stp_mode_;
    uint64_t trade_id_counter_;
};

}  // namespace hft
