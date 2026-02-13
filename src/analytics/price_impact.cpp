#include "analytics/price_impact.h"

#include <cmath>

#include <nlohmann/json.hpp>

namespace hft {

PriceImpact::PriceImpact(size_t regression_window)
    : regression_window_(regression_window) {}

void PriceImpact::on_event(const EventMessage& event, const OrderBook& book,
                            Side aggressor_side) {
    // Cache pre-trade mid on every event
    Price current_mid = book.mid_price();

    if (event.type != EventType::Trade) {
        if (current_mid > 0) prev_mid_ = current_mid;
        return;
    }

    // Need valid pre-trade and post-trade mid
    if (prev_mid_ <= 0 || current_mid <= 0) {
        prev_mid_ = current_mid;
        return;
    }

    // Compute delta mid in bps
    double delta_mid_bps = (static_cast<double>(current_mid) -
                            static_cast<double>(prev_mid_)) /
                           static_cast<double>(prev_mid_) * 10000.0;

    // Signed flow: positive for buys, negative for sells
    auto qty = static_cast<double>(event.data.trade.quantity);
    double signed_flow = (aggressor_side == Side::Buy) ? qty : -qty;

    // Evict oldest if window full
    if (samples_.size() >= regression_window_) {
        const auto& oldest = samples_.front();
        sum_x_ -= oldest.signed_flow;
        sum_y_ -= oldest.delta_mid_bps;
        sum_xx_ -= oldest.signed_flow * oldest.signed_flow;
        sum_xy_ -= oldest.signed_flow * oldest.delta_mid_bps;
        sum_abs_y_ -= std::abs(oldest.delta_mid_bps);
        samples_.pop_front();
    }

    // Add new sample
    samples_.push_back({delta_mid_bps, signed_flow});
    sum_x_ += signed_flow;
    sum_y_ += delta_mid_bps;
    sum_xx_ += signed_flow * signed_flow;
    sum_xy_ += signed_flow * delta_mid_bps;
    sum_abs_y_ += std::abs(delta_mid_bps);

    // Permanent impact: compare current mid with mid PERMANENT_LAG trades ago
    mid_history_.push_back(current_mid);
    if (mid_history_.size() > PERMANENT_LAG) {
        Price lagged_mid = mid_history_.front();
        mid_history_.pop_front();
        if (lagged_mid > 0) {
            double perm_bps = std::abs(
                (static_cast<double>(current_mid) -
                 static_cast<double>(lagged_mid)) /
                static_cast<double>(lagged_mid) * 10000.0);
            permanent_impact_sum_ += perm_bps;
            ++permanent_impact_count_;
        }
    }

    prev_mid_ = current_mid;
}

double PriceImpact::kyle_lambda() const {
    size_t n = samples_.size();
    if (n < 10) return std::nan("");

    auto n_d = static_cast<double>(n);
    // Var(X) = E[X^2] - E[X]^2 = (sum_xx / n) - (sum_x / n)^2
    double var_x = (sum_xx_ / n_d) - (sum_x_ / n_d) * (sum_x_ / n_d);
    if (var_x <= 0.0) return std::nan("");

    // Cov(X, Y) = E[XY] - E[X]*E[Y]
    double cov_xy = (sum_xy_ / n_d) - (sum_x_ / n_d) * (sum_y_ / n_d);

    return cov_xy / var_x;
}

double PriceImpact::avg_temporary_impact_bps() const {
    if (samples_.empty()) return 0.0;
    return sum_abs_y_ / static_cast<double>(samples_.size());
}

double PriceImpact::avg_permanent_impact_bps() const {
    if (permanent_impact_count_ == 0) return 0.0;
    return permanent_impact_sum_ / static_cast<double>(permanent_impact_count_);
}

nlohmann::json PriceImpact::to_json() const {
    nlohmann::json j;
    double lambda = kyle_lambda();
    j["kyle_lambda"] = std::isnan(lambda) ? nullptr : nlohmann::json(lambda);
    j["avg_temporary_impact_bps"] = avg_temporary_impact_bps();
    j["avg_permanent_impact_bps"] = avg_permanent_impact_bps();
    j["sample_count"] = samples_.size();
    j["regression_window"] = regression_window_;
    return j;
}

}  // namespace hft
