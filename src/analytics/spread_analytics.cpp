#include "analytics/spread_analytics.h"

#include <algorithm>
#include <cmath>

#include <nlohmann/json.hpp>

namespace hft {

void SpreadAnalytics::on_event(const EventMessage& event, const OrderBook& book) {
    // Cache previous mid before updating
    prev_mid_ = current_mid_;

    // Sample current spread and mid
    current_spread_ = book.spread();
    current_mid_ = book.mid_price();

    // Update spread stats if both sides present
    if (current_spread_ >= 0 && current_mid_ > 0) {
        double bps = static_cast<double>(current_spread_) /
                     static_cast<double>(current_mid_) * 10000.0;
        spread_bps_sum_ += bps;
        ++spread_samples_;
        if (spread_samples_ == 1) {
            min_spread_bps_ = bps;
            max_spread_bps_ = bps;
        } else {
            min_spread_bps_ = std::min(min_spread_bps_, bps);
            max_spread_bps_ = std::max(max_spread_bps_, bps);
        }
    }

    // On trade: compute effective spread
    if (event.type == EventType::Trade && prev_mid_ > 0) {
        Price trade_price = event.data.trade.price;
        Price diff = trade_price - prev_mid_;
        last_effective_spread_ = 2 * std::abs(diff);

        double eff_bps = static_cast<double>(last_effective_spread_) /
                         static_cast<double>(prev_mid_) * 10000.0;
        effective_spread_bps_sum_ += eff_bps;
        ++effective_spread_count_;
    }
}

double SpreadAnalytics::current_spread_bps() const {
    if (current_mid_ <= 0 || current_spread_ < 0) return 0.0;
    return static_cast<double>(current_spread_) /
           static_cast<double>(current_mid_) * 10000.0;
}

double SpreadAnalytics::avg_spread_bps() const {
    if (spread_samples_ == 0) return 0.0;
    return spread_bps_sum_ / static_cast<double>(spread_samples_);
}

double SpreadAnalytics::avg_effective_spread_bps() const {
    if (effective_spread_count_ == 0) return 0.0;
    return effective_spread_bps_sum_ / static_cast<double>(effective_spread_count_);
}

nlohmann::json SpreadAnalytics::to_json() const {
    nlohmann::json j;
    j["current_spread_bps"] = current_spread_bps();
    j["avg_spread_bps"] = avg_spread_bps();
    j["min_spread_bps"] = (spread_samples_ > 0) ? min_spread_bps_ : 0.0;
    j["max_spread_bps"] = (spread_samples_ > 0) ? max_spread_bps_ : 0.0;
    j["spread_samples"] = spread_samples_;
    j["avg_effective_spread_bps"] = avg_effective_spread_bps();
    j["effective_spread_count"] = effective_spread_count_;
    return j;
}

}  // namespace hft
