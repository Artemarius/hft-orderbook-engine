#pragma once

/// @file order_flow_imbalance.h
/// @brief Rolling order flow imbalance tracker.

#include <cstddef>
#include <deque>

#include <nlohmann/json_fwd.hpp>

#include "core/types.h"
#include "orderbook/order_book.h"
#include "transport/message.h"

namespace hft {

/// Tracks rolling buy/sell volume imbalance over a configurable window.
///
/// On Trade: adds {side, quantity} to rolling deque, maintains running sums.
/// Evicts oldest entry when window is full.
/// Imbalance = (buy_vol - sell_vol) / (buy_vol + sell_vol), range [-1, +1].
class OrderFlowImbalance {
public:
    explicit OrderFlowImbalance(size_t window_size = 100);

    /// Process an event. Only trades affect imbalance.
    /// @param aggressor_side  The inferred aggressor side (from Lee-Ready tick test).
    void on_event(const EventMessage& event, const OrderBook& book,
                  Side aggressor_side);

    /// Current imbalance in [-1, +1]. 0 if no samples.
    [[nodiscard]] double current_imbalance() const;

    /// Number of trade samples currently in the window.
    [[nodiscard]] size_t sample_count() const { return samples_.size(); }

    /// Serialize metrics to JSON.
    [[nodiscard]] nlohmann::json to_json() const;

private:
    struct FlowSample {
        Side side;
        Quantity quantity;
    };

    size_t window_size_;
    std::deque<FlowSample> samples_;
    double buy_vol_ = 0.0;
    double sell_vol_ = 0.0;
};

}  // namespace hft
