#include "orderbook/order_book.h"

#include <cstdlib>
#include <cstring>

namespace hft {

OrderBook::OrderBook(Price min_price, Price max_price, Price tick_size,
                     size_t max_orders)
    : bid_levels_(nullptr),
      ask_levels_(nullptr),
      num_levels_(0),
      min_price_(min_price),
      max_price_(max_price),
      tick_size_(tick_size),
      best_bid_idx_(INVALID_INDEX),
      best_ask_idx_(INVALID_INDEX),
      order_map_(max_orders),
      order_count_(0) {
    num_levels_ =
        static_cast<size_t>((max_price - min_price) / tick_size) + 1;

    // calloc zero-inits: price=0, total_quantity=0, order_count=0,
    // head=nullptr, tail=nullptr — a valid empty PriceLevel.
    bid_levels_ =
        static_cast<PriceLevel*>(std::calloc(num_levels_, sizeof(PriceLevel)));
    ask_levels_ =
        static_cast<PriceLevel*>(std::calloc(num_levels_, sizeof(PriceLevel)));

    if (!bid_levels_ || !ask_levels_) {
        std::abort();
    }
}

OrderBook::~OrderBook() {
    std::free(bid_levels_);
    std::free(ask_levels_);
}

// ---------------------------------------------------------------------------
// Add / Cancel / Remove
// ---------------------------------------------------------------------------

AddResult OrderBook::add_order(Order* order) noexcept {
    if (!is_valid_price(order->price)) [[unlikely]] {
        return {false};
    }

    if (!order_map_.insert(order->order_id, order)) [[unlikely]] {
        return {false};  // Duplicate ID or ID == 0
    }

    size_t idx = price_to_index(order->price);
    PriceLevel* levels =
        (order->side == Side::Buy) ? bid_levels_ : ask_levels_;

    levels[idx].price = order->price;
    levels[idx].add_order(order);
    ++order_count_;

    // Update best bid/ask
    if (order->side == Side::Buy) {
        if (best_bid_idx_ == INVALID_INDEX || idx > best_bid_idx_) {
            best_bid_idx_ = idx;
        }
    } else {
        if (best_ask_idx_ == INVALID_INDEX || idx < best_ask_idx_) {
            best_ask_idx_ = idx;
        }
    }

    order->status = OrderStatus::Accepted;
    return {true};
}

CancelResult OrderBook::cancel_order(OrderId id) noexcept {
    Order* order = order_map_.find(id);
    if (!order) [[unlikely]] {
        return {false, nullptr};
    }

    remove_order(order);
    order->status = OrderStatus::Cancelled;
    return {true, order};
}

void OrderBook::remove_order(Order* order) noexcept {
    size_t idx = price_to_index(order->price);
    PriceLevel* levels =
        (order->side == Side::Buy) ? bid_levels_ : ask_levels_;

    levels[idx].remove_order(order);
    order_map_.erase(order->order_id);
    --order_count_;

    // If this level is now empty and was the best, find the new best.
    if (levels[idx].empty()) {
        if (order->side == Side::Buy && idx == best_bid_idx_) {
            update_best_bid_after_remove(idx);
        } else if (order->side == Side::Sell && idx == best_ask_idx_) {
            update_best_ask_after_remove(idx);
        }
    }
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

const PriceLevel* OrderBook::best_bid() const noexcept {
    if (best_bid_idx_ == INVALID_INDEX) return nullptr;
    return &bid_levels_[best_bid_idx_];
}

const PriceLevel* OrderBook::best_ask() const noexcept {
    if (best_ask_idx_ == INVALID_INDEX) return nullptr;
    return &ask_levels_[best_ask_idx_];
}

PriceLevel* OrderBook::best_bid_level() noexcept {
    if (best_bid_idx_ == INVALID_INDEX) return nullptr;
    return &bid_levels_[best_bid_idx_];
}

PriceLevel* OrderBook::best_ask_level() noexcept {
    if (best_ask_idx_ == INVALID_INDEX) return nullptr;
    return &ask_levels_[best_ask_idx_];
}

Price OrderBook::spread() const noexcept {
    if (best_bid_idx_ == INVALID_INDEX || best_ask_idx_ == INVALID_INDEX) {
        return -1;
    }
    return ask_levels_[best_ask_idx_].price - bid_levels_[best_bid_idx_].price;
}

Price OrderBook::mid_price() const noexcept {
    if (best_bid_idx_ == INVALID_INDEX || best_ask_idx_ == INVALID_INDEX) {
        return 0;
    }
    return (bid_levels_[best_bid_idx_].price +
            ask_levels_[best_ask_idx_].price) / 2;
}

Order* OrderBook::find_order(OrderId id) const noexcept {
    return order_map_.find(id);
}

// ---------------------------------------------------------------------------
// Price indexing
// ---------------------------------------------------------------------------

size_t OrderBook::price_to_index(Price price) const noexcept {
    return static_cast<size_t>((price - min_price_) / tick_size_);
}

Price OrderBook::index_to_price(size_t index) const noexcept {
    return min_price_ + static_cast<Price>(index) * tick_size_;
}

bool OrderBook::is_valid_price(Price price) const noexcept {
    return price >= min_price_ && price <= max_price_ &&
           ((price - min_price_) % tick_size_ == 0);
}

// ---------------------------------------------------------------------------
// FOK feasibility
// ---------------------------------------------------------------------------

Quantity OrderBook::available_quantity(Side side, Price limit_price) const noexcept {
    Quantity total = 0;

    if (side == Side::Sell) {
        // Buying: walk ask levels from best_ask upward to limit_price
        if (best_ask_idx_ == INVALID_INDEX) return 0;
        size_t max_idx = price_to_index(
            (limit_price > max_price_) ? max_price_ : limit_price);
        for (size_t i = best_ask_idx_; i <= max_idx && i < num_levels_; ++i) {
            total += ask_levels_[i].total_quantity;
        }
    } else {
        // Selling: walk bid levels from best_bid downward to limit_price
        if (best_bid_idx_ == INVALID_INDEX) return 0;
        size_t min_idx = price_to_index(
            (limit_price < min_price_) ? min_price_ : limit_price);
        for (size_t i = best_bid_idx_;; --i) {
            total += bid_levels_[i].total_quantity;
            if (i == min_idx || i == 0) break;
        }
    }

    return total;
}

// ---------------------------------------------------------------------------
// Depth queries
// ---------------------------------------------------------------------------

size_t OrderBook::get_bid_depth(DepthEntry* out, size_t max_levels) const noexcept {
    if (best_bid_idx_ == INVALID_INDEX || max_levels == 0) return 0;

    size_t count = 0;
    for (size_t i = best_bid_idx_;; --i) {
        if (!bid_levels_[i].empty()) {
            out[count].price = bid_levels_[i].price;
            out[count].quantity = bid_levels_[i].total_quantity;
            out[count].order_count = bid_levels_[i].order_count;
            ++count;
            if (count >= max_levels) break;
        }
        if (i == 0) break;
    }
    return count;
}

size_t OrderBook::get_ask_depth(DepthEntry* out, size_t max_levels) const noexcept {
    if (best_ask_idx_ == INVALID_INDEX || max_levels == 0) return 0;

    size_t count = 0;
    for (size_t i = best_ask_idx_; i < num_levels_; ++i) {
        if (!ask_levels_[i].empty()) {
            out[count].price = ask_levels_[i].price;
            out[count].quantity = ask_levels_[i].total_quantity;
            out[count].order_count = ask_levels_[i].order_count;
            ++count;
            if (count >= max_levels) break;
        }
    }
    return count;
}

// ---------------------------------------------------------------------------
// Best bid/ask maintenance
// ---------------------------------------------------------------------------

void OrderBook::update_best_bid_after_remove(size_t emptied_idx) noexcept {
    // Best bid = highest index with orders. Scan down from emptied_idx.
    if (emptied_idx == 0) {
        best_bid_idx_ = INVALID_INDEX;
        return;
    }
    for (size_t i = emptied_idx - 1;; --i) {
        if (!bid_levels_[i].empty()) {
            best_bid_idx_ = i;
            return;
        }
        if (i == 0) break;
    }
    best_bid_idx_ = INVALID_INDEX;
}

void OrderBook::update_best_ask_after_remove(size_t emptied_idx) noexcept {
    // Best ask = lowest index with orders. Scan up from emptied_idx.
    for (size_t i = emptied_idx + 1; i < num_levels_; ++i) {
        if (!ask_levels_[i].empty()) {
            best_ask_idx_ = i;
            return;
        }
    }
    best_ask_idx_ = INVALID_INDEX;
}

}  // namespace hft
