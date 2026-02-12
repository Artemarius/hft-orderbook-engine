#pragma once

/// @file depth_profile.h
/// @brief Order book depth profile analysis.

#include <cstddef>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "core/types.h"
#include "orderbook/order_book.h"
#include "transport/message.h"

namespace hft {

/// Tracks cumulative depth profile and depth imbalance across top N price levels.
///
/// On each event: calls book.get_bid_depth() / book.get_ask_depth() to walk
/// levels from BBO. Stores cumulative quantity per level and tracks average
/// depth profile across all snapshots.
class DepthProfile {
public:
    explicit DepthProfile(size_t max_levels = 10);

    /// Process an event and snapshot depth profile.
    void on_event(const EventMessage& event, const OrderBook& book);

    /// Current bid depth (quantity per level, ordered best to worst).
    [[nodiscard]] const std::vector<Quantity>& bid_depth() const { return bid_depth_; }

    /// Current ask depth (quantity per level, ordered best to worst).
    [[nodiscard]] const std::vector<Quantity>& ask_depth() const { return ask_depth_; }

    /// Depth imbalance = (sum_bid - sum_ask) / (sum_bid + sum_ask) over top N levels.
    /// Range [-1, +1]. Returns 0 if both sides are empty.
    [[nodiscard]] double depth_imbalance() const;

    /// Serialize metrics to JSON.
    [[nodiscard]] nlohmann::json to_json() const;

private:
    size_t max_levels_;
    std::vector<Quantity> bid_depth_;
    std::vector<Quantity> ask_depth_;

    // Average depth tracking
    std::vector<double> avg_bid_depth_;
    std::vector<double> avg_ask_depth_;
    uint64_t snapshot_count_ = 0;
};

}  // namespace hft
