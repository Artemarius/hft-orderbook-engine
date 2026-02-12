#include "analytics/microprice_calculator.h"

#include <nlohmann/json.hpp>

namespace hft {

void MicropriceCalculator::on_event(const EventMessage& /*event*/,
                                     const OrderBook& book) {
    const PriceLevel* bid = book.best_bid();
    const PriceLevel* ask = book.best_ask();

    if (!bid || !ask) {
        valid_ = false;
        return;
    }

    double bid_qty = static_cast<double>(bid->total_quantity);
    double ask_qty = static_cast<double>(ask->total_quantity);
    double bid_px = static_cast<double>(bid->price);
    double ask_px = static_cast<double>(ask->price);

    double total_qty = bid_qty + ask_qty;
    if (total_qty <= 0.0) {
        valid_ = false;
        return;
    }

    microprice_ = (bid_qty * ask_px + ask_qty * bid_px) / total_qty;
    valid_ = true;
}

nlohmann::json MicropriceCalculator::to_json() const {
    nlohmann::json j;
    j["microprice"] = valid_ ? microprice_ / static_cast<double>(PRICE_SCALE) : 0.0;
    j["valid"] = valid_;
    return j;
}

}  // namespace hft
