#pragma once

/// @file price_impact.h
/// @brief Price impact analysis: Kyle's Lambda, temporary and permanent impact.

#include <cstddef>
#include <deque>

#include <nlohmann/json_fwd.hpp>

#include "core/types.h"
#include "orderbook/order_book.h"
#include "transport/message.h"

namespace hft {

/// Estimates price impact from trade data using OLS regression.
///
/// On Trade: records {delta_mid_bps, signed_flow} in a rolling window.
/// - delta_mid = (new_mid - prev_mid) / prev_mid * 10000 (bps)
/// - signed_flow = quantity * (buy ? +1 : -1)
///
/// Kyle's Lambda = Cov(delta_mid, signed_flow) / Var(signed_flow)
///
/// Temporary impact: immediate mid change per trade (average |delta_mid|).
/// Permanent impact: mid change persisting after N trades (average).
class PriceImpact {
public:
    explicit PriceImpact(size_t regression_window = 200);

    /// Process an event with the inferred aggressor side.
    void on_event(const EventMessage& event, const OrderBook& book,
                  Side aggressor_side);

    /// Kyle's Lambda (price impact coefficient). NaN if < 10 observations.
    [[nodiscard]] double kyle_lambda() const;

    /// Average absolute immediate mid change in bps per trade.
    [[nodiscard]] double avg_temporary_impact_bps() const;

    /// Average permanent impact in bps (mid change persisting 5 trades later).
    [[nodiscard]] double avg_permanent_impact_bps() const;

    /// Serialize metrics to JSON.
    [[nodiscard]] nlohmann::json to_json() const;

private:
    struct ImpactSample {
        double delta_mid_bps;
        double signed_flow;
    };

    size_t regression_window_;
    std::deque<ImpactSample> samples_;

    // Running sums for OLS
    double sum_x_ = 0.0;   // sum of signed_flow
    double sum_y_ = 0.0;   // sum of delta_mid_bps
    double sum_xx_ = 0.0;  // sum of signed_flow^2
    double sum_xy_ = 0.0;  // sum of signed_flow * delta_mid_bps
    double sum_abs_y_ = 0.0; // sum of |delta_mid_bps| for temporary impact

    // Pre-trade mid price caching
    Price prev_mid_ = 0;

    // For permanent impact: track mid prices to compare with N-trade-ago mid
    static constexpr size_t PERMANENT_LAG = 5;
    std::deque<Price> mid_history_;
    double permanent_impact_sum_ = 0.0;
    uint64_t permanent_impact_count_ = 0;
};

}  // namespace hft
