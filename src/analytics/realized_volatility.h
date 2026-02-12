#pragma once

/// @file realized_volatility.h
/// @brief Tick-level and time-bar realized volatility estimators.

#include <cstddef>
#include <cstdint>
#include <deque>

#include <nlohmann/json_fwd.hpp>

#include "core/types.h"
#include "orderbook/order_book.h"
#include "transport/message.h"

namespace hft {

/// Computes realized volatility from trade prices (tick-level) and from
/// time-bar mid price samples.
///
/// Tick-level: On each Trade, compute log(price / prev_trade_price), store in
///   deque. Vol = sqrt(sum(r^2)) over the window.
///
/// Time-bar: When timestamp crosses bar boundary, sample mid price, compute
///   log return. Vol = sqrt(sum(r^2)) over the window.
class RealizedVolatility {
public:
    /// @param tick_window   Max number of tick log returns to keep.
    /// @param time_bar_ns   Time bar width in nanoseconds (default 1 second).
    explicit RealizedVolatility(size_t tick_window = 50,
                                uint64_t time_bar_ns = 1'000'000'000);

    /// Process an event and update volatility estimates.
    void on_event(const EventMessage& event, const OrderBook& book);

    /// Tick-level realized volatility (sqrt of sum of squared log returns).
    [[nodiscard]] double tick_volatility() const;

    /// Time-bar realized volatility (sqrt of sum of squared log returns).
    [[nodiscard]] double time_bar_volatility() const;

    /// Serialize metrics to JSON.
    [[nodiscard]] nlohmann::json to_json() const;

private:
    size_t tick_window_;
    uint64_t time_bar_ns_;

    // Tick-level state
    double prev_trade_price_ = 0.0;
    std::deque<double> tick_returns_;
    double tick_return_sq_sum_ = 0.0;

    // Time-bar state
    uint64_t current_bar_start_ = 0;
    double bar_start_mid_ = 0.0;
    bool bar_initialized_ = false;
    std::deque<double> bar_returns_;
    double bar_return_sq_sum_ = 0.0;
};

}  // namespace hft
