#pragma once

/// @file analytics_engine.h
/// @brief Orchestrator for all analytics modules with JSON/CSV output.

#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "analytics/analytics_config.h"
#include "analytics/depth_profile.h"
#include "analytics/microprice_calculator.h"
#include "analytics/order_flow_imbalance.h"
#include "analytics/price_impact.h"
#include "analytics/realized_volatility.h"
#include "analytics/spread_analytics.h"
#include "core/types.h"
#include "orderbook/order_book.h"
#include "transport/message.h"

namespace hft {

/// A single time-series row captured on each Trade event.
struct TimeSeriesRow {
    uint64_t sequence_num;
    Timestamp timestamp;
    Price trade_price;
    Quantity trade_quantity;
    Price spread;
    double spread_bps;
    double microprice;
    double imbalance;
    double tick_vol;
    double depth_imbalance;
    Side aggressor_side;
};

/// Orchestrates all 6 analytics modules. Single callback to register with
/// ReplayEngine. Infers aggressor side via Lee-Ready tick test, dispatches
/// to all modules, captures time-series rows for CSV output.
class AnalyticsEngine {
public:
    /// @param book   Reference to the order book (must outlive this object).
    /// @param config Configuration for all modules.
    AnalyticsEngine(const OrderBook& book,
                    const AnalyticsConfig& config = {});

    /// Process an event — dispatches to all modules.
    void on_event(const EventMessage& event);

    /// Write aggregate JSON summary to file.
    void write_json(const std::string& path) const;

    /// Write time-series CSV (one row per trade) to file.
    void write_csv(const std::string& path) const;

    /// Print human-readable summary to stdout.
    void print_summary() const;

    /// Aggregate JSON of all modules.
    [[nodiscard]] nlohmann::json to_json() const;

    // --- Module accessors ---
    [[nodiscard]] const SpreadAnalytics& spread() const { return spread_; }
    [[nodiscard]] const MicropriceCalculator& microprice() const { return microprice_; }
    [[nodiscard]] const OrderFlowImbalance& order_flow() const { return order_flow_; }
    [[nodiscard]] const RealizedVolatility& volatility() const { return volatility_; }
    [[nodiscard]] const PriceImpact& price_impact() const { return price_impact_; }
    [[nodiscard]] const DepthProfile& depth() const { return depth_; }

    [[nodiscard]] size_t trade_count() const { return time_series_.size(); }

private:
    /// Infer aggressor side using Lee-Ready tick test.
    [[nodiscard]] Side infer_aggressor(Price trade_price) const;

    const OrderBook& book_;
    AnalyticsConfig config_;

    SpreadAnalytics spread_;
    MicropriceCalculator microprice_;
    OrderFlowImbalance order_flow_;
    RealizedVolatility volatility_;
    PriceImpact price_impact_;
    DepthProfile depth_;

    // Lee-Ready state
    Price prev_mid_ = 0;

    // Time series
    std::vector<TimeSeriesRow> time_series_;
};

}  // namespace hft
