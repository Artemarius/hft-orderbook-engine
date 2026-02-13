#include "analytics/order_flow_imbalance.h"

#include <nlohmann/json.hpp>

namespace hft {

OrderFlowImbalance::OrderFlowImbalance(size_t window_size)
    : window_size_(window_size) {}

void OrderFlowImbalance::on_event(const EventMessage& event,
                                   const OrderBook& /*book*/,
                                   Side aggressor_side) {
    if (event.type != EventType::Trade) return;

    Quantity qty = event.data.trade.quantity;
    auto qty_d = static_cast<double>(qty);

    // Evict oldest if window full
    if (samples_.size() >= window_size_) {
        const auto& oldest = samples_.front();
        if (oldest.side == Side::Buy) {
            buy_vol_ -= static_cast<double>(oldest.quantity);
        } else {
            sell_vol_ -= static_cast<double>(oldest.quantity);
        }
        samples_.pop_front();
    }

    // Add new sample
    samples_.push_back({aggressor_side, qty});
    if (aggressor_side == Side::Buy) {
        buy_vol_ += qty_d;
    } else {
        sell_vol_ += qty_d;
    }
}

double OrderFlowImbalance::current_imbalance() const {
    double total = buy_vol_ + sell_vol_;
    if (total <= 0.0) return 0.0;
    return (buy_vol_ - sell_vol_) / total;
}

nlohmann::json OrderFlowImbalance::to_json() const {
    nlohmann::json j;
    j["current_imbalance"] = current_imbalance();
    j["sample_count"] = samples_.size();
    j["window_size"] = window_size_;
    j["buy_volume"] = buy_vol_;
    j["sell_volume"] = sell_vol_;
    return j;
}

}  // namespace hft
