#include "analytics/analytics_engine.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>

#include <nlohmann/json.hpp>

namespace hft {

AnalyticsEngine::AnalyticsEngine(const OrderBook& book,
                                   const AnalyticsConfig& config)
    : book_(book),
      config_(config),
      order_flow_(config.imbalance_window),
      volatility_(config.vol_tick_window, config.vol_time_bar_ns),
      price_impact_(config.impact_regression_window),
      depth_(config.depth_max_levels) {}

Side AnalyticsEngine::infer_aggressor(Price trade_price) const {
    // Lee-Ready tick test: trade at or above mid => buyer-initiated
    if (prev_mid_ <= 0) return Side::Buy;  // default when no mid available
    return (trade_price >= prev_mid_) ? Side::Buy : Side::Sell;
}

void AnalyticsEngine::on_event(const EventMessage& event) {
    // Cache pre-trade mid for Lee-Ready
    Price current_mid = book_.mid_price();

    // Infer aggressor side before dispatching (only meaningful for trades)
    Side aggressor = Side::Buy;
    if (event.type == EventType::Trade) {
        aggressor = infer_aggressor(event.data.trade.price);
    }

    // Dispatch to all modules
    spread_.on_event(event, book_);
    microprice_.on_event(event, book_);
    order_flow_.on_event(event, book_, aggressor);
    volatility_.on_event(event, book_);
    price_impact_.on_event(event, book_, aggressor);
    depth_.on_event(event, book_);

    // Capture time-series row on trade
    if (event.type == EventType::Trade) {
        TimeSeriesRow row{};
        row.sequence_num = event.sequence_num;
        row.timestamp = event.data.trade.timestamp;
        row.trade_price = event.data.trade.price;
        row.trade_quantity = event.data.trade.quantity;
        row.spread = book_.spread();
        row.spread_bps = spread_.current_spread_bps();
        row.microprice = microprice_.is_valid() ? microprice_.current_microprice() : 0.0;
        row.imbalance = order_flow_.current_imbalance();
        row.tick_vol = volatility_.tick_volatility();
        row.depth_imbalance = depth_.depth_imbalance();
        row.aggressor_side = aggressor;
        time_series_.push_back(row);
    }

    // Update prev_mid after processing
    if (current_mid > 0) {
        prev_mid_ = current_mid;
    }
}

nlohmann::json AnalyticsEngine::to_json() const {
    nlohmann::json j;
    j["spread"] = spread_.to_json();
    j["microprice"] = microprice_.to_json();
    j["order_flow_imbalance"] = order_flow_.to_json();
    j["realized_volatility"] = volatility_.to_json();
    j["price_impact"] = price_impact_.to_json();
    j["depth_profile"] = depth_.to_json();
    j["trade_count"] = time_series_.size();
    return j;
}

void AnalyticsEngine::write_json(const std::string& path) const {
    std::ofstream out(path);
    if (!out.is_open()) {
        std::cerr << "Failed to write analytics JSON to: " << path << "\n";
        return;
    }
    out << to_json().dump(2) << "\n";
}

void AnalyticsEngine::write_csv(const std::string& path) const {
    std::ofstream out(path);
    if (!out.is_open()) {
        std::cerr << "Failed to write analytics CSV to: " << path << "\n";
        return;
    }

    // Header
    out << "sequence_num,timestamp,trade_price,trade_quantity,spread,"
        << "spread_bps,microprice,imbalance,tick_vol,depth_imbalance,"
        << "aggressor_side\n";

    auto price_to_double = [](Price p) -> double {
        return static_cast<double>(p) / static_cast<double>(PRICE_SCALE);
    };

    out << std::fixed << std::setprecision(8);
    for (const auto& row : time_series_) {
        out << row.sequence_num << ","
            << row.timestamp << ","
            << price_to_double(row.trade_price) << ","
            << row.trade_quantity << ","
            << price_to_double(row.spread) << ","
            << row.spread_bps << ","
            << (row.microprice / static_cast<double>(PRICE_SCALE)) << ","
            << row.imbalance << ","
            << row.tick_vol << ","
            << row.depth_imbalance << ","
            << (row.aggressor_side == Side::Buy ? "BUY" : "SELL")
            << "\n";
    }
}

void AnalyticsEngine::print_summary() const {
    auto price_to_double = [](Price p) -> double {
        return static_cast<double>(p) / static_cast<double>(PRICE_SCALE);
    };

    std::cout << "\n=== Analytics Summary ===\n";

    std::cout << "\nSpread:\n";
    std::cout << "  Current:   " << spread_.current_spread_bps() << " bps\n";
    std::cout << "  Average:   " << spread_.avg_spread_bps() << " bps\n";
    std::cout << "  Effective: " << spread_.avg_effective_spread_bps() << " bps\n";

    std::cout << "\nMicroprice:\n";
    if (microprice_.is_valid()) {
        std::cout << "  Value: $"
                  << microprice_.current_microprice() / static_cast<double>(PRICE_SCALE)
                  << "\n";
    } else {
        std::cout << "  Value: N/A (empty side)\n";
    }

    std::cout << "\nOrder Flow Imbalance:\n";
    std::cout << "  Current:  " << order_flow_.current_imbalance() << "\n";
    std::cout << "  Samples:  " << order_flow_.sample_count() << "\n";

    std::cout << "\nRealized Volatility:\n";
    std::cout << "  Tick:     " << volatility_.tick_volatility() << "\n";
    std::cout << "  Time-bar: " << volatility_.time_bar_volatility() << "\n";

    std::cout << "\nPrice Impact:\n";
    double lambda = price_impact_.kyle_lambda();
    if (std::isnan(lambda)) {
        std::cout << "  Kyle's Lambda: N/A (insufficient data)\n";
    } else {
        std::cout << "  Kyle's Lambda: " << lambda << "\n";
    }
    std::cout << "  Temp impact:   " << price_impact_.avg_temporary_impact_bps() << " bps\n";
    std::cout << "  Perm impact:   " << price_impact_.avg_permanent_impact_bps() << " bps\n";

    std::cout << "\nDepth Profile:\n";
    std::cout << "  Imbalance: " << depth_.depth_imbalance() << "\n";
    std::cout << "  Bid levels: " << depth_.bid_depth().size() << "\n";
    std::cout << "  Ask levels: " << depth_.ask_depth().size() << "\n";

    (void)price_to_double;  // suppress unused warning when not needed
}

}  // namespace hft
