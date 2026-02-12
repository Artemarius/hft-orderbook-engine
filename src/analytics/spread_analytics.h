#pragma once

/// @file spread_analytics.h
/// @brief Bid-ask spread and effective spread analytics.

#include <cstdint>

#include <nlohmann/json_fwd.hpp>

#include "core/types.h"
#include "orderbook/order_book.h"
#include "transport/message.h"

namespace hft {

/// Tracks bid-ask spread and effective spread metrics.
///
/// On every event: samples book.spread() and book.mid_price(), updates running stats.
/// On Trade: computes effective spread = 2 * |trade_price - prev_mid|.
class SpreadAnalytics {
public:
    /// Process an event and update spread metrics.
    void on_event(const EventMessage& event, const OrderBook& book);

    /// Current bid-ask spread (fixed-point). Returns -1 if either side empty.
    [[nodiscard]] Price current_spread() const { return current_spread_; }

    /// Current spread in basis points. Returns 0 if mid price is 0.
    [[nodiscard]] double current_spread_bps() const;

    /// Most recent effective spread (fixed-point). 0 if no trades yet.
    [[nodiscard]] Price last_effective_spread() const { return last_effective_spread_; }

    /// Average bid-ask spread in basis points across all samples.
    [[nodiscard]] double avg_spread_bps() const;

    /// Average effective spread in basis points across all trades.
    [[nodiscard]] double avg_effective_spread_bps() const;

    /// Serialize metrics to JSON.
    [[nodiscard]] nlohmann::json to_json() const;

private:
    Price current_spread_ = -1;
    Price current_mid_ = 0;
    Price prev_mid_ = 0;
    Price last_effective_spread_ = 0;

    // Running stats for spread
    double spread_bps_sum_ = 0.0;
    uint64_t spread_samples_ = 0;
    double min_spread_bps_ = 0.0;
    double max_spread_bps_ = 0.0;

    // Running stats for effective spread
    double effective_spread_bps_sum_ = 0.0;
    uint64_t effective_spread_count_ = 0;
};

}  // namespace hft
