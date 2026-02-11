#include "matching/matching_engine.h"

#include <algorithm>

namespace hft {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

MatchingEngine::MatchingEngine(OrderBook& book, MemoryPool<Order>& pool,
                               SelfTradePreventionMode stp) noexcept
    : book_(book), pool_(pool), stp_mode_(stp), trade_id_counter_(0) {}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

MatchResult MatchingEngine::submit_order(Order* order) noexcept {
    MatchResult result{};
    result.status = MatchStatus::Rejected;
    result.trade_count = 0;
    result.filled_quantity = 0;
    result.remaining_quantity = order->remaining_quantity();

    // Validate price for non-Market orders
    if (order->type != OrderType::Market) {
        if (!book_.is_valid_price(order->price)) [[unlikely]] {
            order->status = OrderStatus::Rejected;
            pool_.deallocate(order);
            return result;
        }
    }

    // FOK: check feasibility before matching
    if (order->type == OrderType::FOK) {
        if (!check_fok_feasibility(order)) [[unlikely]] {
            order->status = OrderStatus::Rejected;
            pool_.deallocate(order);
            return result;
        }
    }

    // Attempt matching
    match_order(order, result);
    result.remaining_quantity = order->remaining_quantity();

    // STP cancelled the aggressive order — deallocate and return
    if (result.status == MatchStatus::SelfTradePrevented) [[unlikely]] {
        pool_.deallocate(order);
        return result;
    }

    if (order->remaining_quantity() == 0) {
        // Fully filled
        result.status = MatchStatus::Filled;
        order->status = OrderStatus::Filled;
        pool_.deallocate(order);
    } else if (order->type == OrderType::Market ||
               order->type == OrderType::IOC) {
        // Market/IOC: cancel unfilled remainder
        result.status = (result.filled_quantity > 0)
                            ? MatchStatus::Cancelled
                            : MatchStatus::Cancelled;
        order->status = OrderStatus::Cancelled;
        pool_.deallocate(order);
    } else if (order->type == OrderType::FOK) {
        // FOK should have been fully filled (feasibility was checked).
        // Defensive: reject if somehow not filled.
        result.status = MatchStatus::Rejected;
        order->status = OrderStatus::Rejected;
        pool_.deallocate(order);
    } else {
        // Limit/GTC/Iceberg: rest remainder on the book
        if (result.filled_quantity > 0) {
            result.status = MatchStatus::PartialFill;
            order->status = OrderStatus::PartialFill;
        } else {
            result.status = MatchStatus::Resting;
        }
        book_.add_order(order);
    }

    return result;
}

bool MatchingEngine::cancel_order(OrderId id) noexcept {
    auto cr = book_.cancel_order(id);
    if (cr.success) {
        pool_.deallocate(cr.order);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Core matching loop
// ---------------------------------------------------------------------------

void MatchingEngine::match_order(Order* order, MatchResult& result) noexcept {
    while (order->remaining_quantity() > 0 &&
           result.trade_count < MAX_TRADES_PER_MATCH) {

        // Get the best opposite-side level
        PriceLevel* level = (order->side == Side::Buy)
                                ? book_.best_ask_level()
                                : book_.best_bid_level();

        if (!level) break;
        if (!price_crosses(order, level)) break;

        // Walk orders at this price level (FIFO)
        while (order->remaining_quantity() > 0 &&
               !level->empty() &&
               result.trade_count < MAX_TRADES_PER_MATCH) {

            Order* resting = level->front();

            // Self-trade prevention
            if (stp_mode_ != SelfTradePreventionMode::None) [[unlikely]] {
                if (check_self_trade(order, resting)) {
                    if (handle_self_trade(order, resting, level, result)) {
                        // Aggressive order was cancelled — stop matching
                        return;
                    }
                    // Resting order was cancelled — continue with next resting
                    continue;
                }
            }

            // Determine fill quantity
            Quantity resting_available = resting->remaining_visible();
            Quantity fill_qty = std::min(order->remaining_quantity(),
                                         resting_available);

            execute_fill(order, resting, fill_qty, level, result);

            // Handle resting order post-fill
            if (resting->remaining_quantity() == 0) {
                // Fully filled — remove from book and deallocate
                book_.remove_order(resting);
                resting->status = OrderStatus::Filled;
                pool_.deallocate(resting);
            } else if (resting->remaining_visible() == 0 &&
                       resting->type == OrderType::Iceberg) {
                // Iceberg visible exhausted — remove, replenish, re-add
                book_.remove_order(resting);
                replenish_iceberg(resting);
                book_.add_order(resting);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// FOK feasibility check
// ---------------------------------------------------------------------------

bool MatchingEngine::check_fok_feasibility(const Order* order) const noexcept {
    // Walk opposite side to see if enough quantity is available.
    // For buy orders: check sell side. For sell orders: check buy side.
    Side opposite = (order->side == Side::Buy) ? Side::Sell : Side::Buy;
    Quantity available = book_.available_quantity(opposite, order->price);
    return available >= order->quantity;
}

// ---------------------------------------------------------------------------
// Fill execution
// ---------------------------------------------------------------------------

void MatchingEngine::execute_fill(Order* aggressive, Order* resting,
                                   Quantity fill_qty, PriceLevel* level,
                                   MatchResult& result) noexcept {
    // Update price level quantity FIRST
    level->total_quantity -= fill_qty;

    // Update order fill quantities
    aggressive->filled_quantity += fill_qty;
    resting->filled_quantity += fill_qty;

    // Update order statuses
    if (aggressive->remaining_quantity() == 0) {
        aggressive->status = OrderStatus::Filled;
    } else {
        aggressive->status = OrderStatus::PartialFill;
    }
    if (resting->remaining_quantity() == 0) {
        resting->status = OrderStatus::Filled;
    } else {
        resting->status = OrderStatus::PartialFill;
    }

    // Generate trade — price = resting order's price (passive price improvement)
    Trade& trade = result.trades[result.trade_count];
    trade.trade_id = next_trade_id();
    trade.buy_order_id = (aggressive->side == Side::Buy)
                             ? aggressive->order_id
                             : resting->order_id;
    trade.sell_order_id = (aggressive->side == Side::Sell)
                              ? aggressive->order_id
                              : resting->order_id;
    trade.price = resting->price;
    trade.quantity = fill_qty;
    trade.timestamp = aggressive->timestamp;

    ++result.trade_count;
    result.filled_quantity += fill_qty;
}

// ---------------------------------------------------------------------------
// Self-trade prevention
// ---------------------------------------------------------------------------

bool MatchingEngine::check_self_trade(const Order* aggressive,
                                       const Order* resting) noexcept {
    return aggressive->participant_id == resting->participant_id;
}

bool MatchingEngine::handle_self_trade(Order* aggressive, Order* resting,
                                        PriceLevel* /*level*/,
                                        MatchResult& result) noexcept {
    switch (stp_mode_) {
        case SelfTradePreventionMode::CancelNewest:
            // Cancel aggressive order — stop matching
            aggressive->status = OrderStatus::Cancelled;
            result.status = MatchStatus::SelfTradePrevented;
            result.remaining_quantity = aggressive->remaining_quantity();
            return true;  // Stop matching

        case SelfTradePreventionMode::CancelOldest:
            // Cancel resting order — continue matching
            book_.remove_order(resting);
            resting->status = OrderStatus::Cancelled;
            pool_.deallocate(resting);
            return false;  // Continue matching

        case SelfTradePreventionMode::CancelBoth:
            // Cancel both
            book_.remove_order(resting);
            resting->status = OrderStatus::Cancelled;
            pool_.deallocate(resting);
            aggressive->status = OrderStatus::Cancelled;
            result.status = MatchStatus::SelfTradePrevented;
            result.remaining_quantity = aggressive->remaining_quantity();
            return true;  // Stop matching

        case SelfTradePreventionMode::None:
        default:
            return false;  // Should not reach here
    }
}

// ---------------------------------------------------------------------------
// Iceberg replenishment
// ---------------------------------------------------------------------------

void MatchingEngine::replenish_iceberg(Order* order) noexcept {
    Quantity remaining = order->remaining_quantity();
    Quantity new_visible = std::min(order->iceberg_slice_qty, remaining);
    // visible_quantity tracks total visible since order creation.
    // After replenishment, the new visible window starts from current filled.
    order->visible_quantity = order->filled_quantity + new_visible;
}

// ---------------------------------------------------------------------------
// Price crossing
// ---------------------------------------------------------------------------

bool MatchingEngine::price_crosses(const Order* order,
                                    const PriceLevel* level) noexcept {
    // Market orders always cross
    if (order->type == OrderType::Market) [[unlikely]] {
        return true;
    }
    if (order->side == Side::Buy) {
        return order->price >= level->price;
    } else {
        return order->price <= level->price;
    }
}

// ---------------------------------------------------------------------------
// Trade ID generation
// ---------------------------------------------------------------------------

uint64_t MatchingEngine::next_trade_id() noexcept {
    return ++trade_id_counter_;
}

}  // namespace hft
