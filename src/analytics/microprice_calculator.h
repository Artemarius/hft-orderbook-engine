#pragma once

/// @file microprice_calculator.h
/// @brief Volume-weighted microprice calculator.

#include <nlohmann/json_fwd.hpp>

#include "core/types.h"
#include "orderbook/order_book.h"
#include "transport/message.h"

namespace hft {

/// Computes volume-weighted microprice from BBO quantities.
///
/// Formula: (bid_qty * ask_px + ask_qty * bid_px) / (bid_qty + ask_qty)
/// All arithmetic in double (weighted average needs fractional precision).
class MicropriceCalculator {
public:
    /// Process an event and recalculate microprice.
    void on_event(const EventMessage& event, const OrderBook& book);

    /// Current microprice as a double (raw Price would lose fractional precision).
    [[nodiscard]] double current_microprice() const { return microprice_; }

    /// True when both bid and ask sides have orders.
    [[nodiscard]] bool is_valid() const { return valid_; }

    /// Serialize metrics to JSON.
    [[nodiscard]] nlohmann::json to_json() const;

private:
    double microprice_ = 0.0;
    bool valid_ = false;
};

}  // namespace hft
