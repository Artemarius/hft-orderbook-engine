#include "analytics/realized_volatility.h"

#include <cmath>

#include <nlohmann/json.hpp>

namespace hft {

RealizedVolatility::RealizedVolatility(size_t tick_window, uint64_t time_bar_ns)
    : tick_window_(tick_window), time_bar_ns_(time_bar_ns) {}

void RealizedVolatility::on_event(const EventMessage& event,
                                   const OrderBook& book) {
    // --- Time-bar volatility: check bar boundaries on every event ---
    if (event.type == EventType::Trade || event.type == EventType::OrderAccepted) {
        // Use trade timestamp for timing
        Timestamp ts = 0;
        if (event.type == EventType::Trade) {
            ts = event.data.trade.timestamp;
        } else {
            ts = event.data.order_event.timestamp;
        }

        if (!bar_initialized_ && ts > 0) {
            current_bar_start_ = ts;
            Price mid = book.mid_price();
            if (mid > 0) {
                bar_start_mid_ = static_cast<double>(mid);
                bar_initialized_ = true;
            }
        } else if (bar_initialized_ && ts >= current_bar_start_ + time_bar_ns_) {
            // Bar boundary crossed — compute return
            Price mid = book.mid_price();
            if (mid > 0 && bar_start_mid_ > 0.0) {
                double new_mid = static_cast<double>(mid);
                double log_ret = std::log(new_mid / bar_start_mid_);

                // Evict oldest if window full
                if (bar_returns_.size() >= tick_window_) {
                    double oldest = bar_returns_.front();
                    bar_return_sq_sum_ -= oldest * oldest;
                    bar_returns_.pop_front();
                }

                bar_returns_.push_back(log_ret);
                bar_return_sq_sum_ += log_ret * log_ret;

                bar_start_mid_ = new_mid;
            }
            current_bar_start_ = ts;
        }
    }

    // --- Tick-level volatility: only on trades ---
    if (event.type != EventType::Trade) return;

    double trade_price = static_cast<double>(event.data.trade.price);
    if (trade_price <= 0.0) return;

    if (prev_trade_price_ > 0.0) {
        double log_ret = std::log(trade_price / prev_trade_price_);

        // Evict oldest if window full
        if (tick_returns_.size() >= tick_window_) {
            double oldest = tick_returns_.front();
            tick_return_sq_sum_ -= oldest * oldest;
            tick_returns_.pop_front();
        }

        tick_returns_.push_back(log_ret);
        tick_return_sq_sum_ += log_ret * log_ret;
    }

    prev_trade_price_ = trade_price;
}

double RealizedVolatility::tick_volatility() const {
    if (tick_returns_.empty()) return 0.0;
    // Guard against floating-point drift making sum slightly negative
    return std::sqrt(std::max(0.0, tick_return_sq_sum_));
}

double RealizedVolatility::time_bar_volatility() const {
    if (bar_returns_.empty()) return 0.0;
    return std::sqrt(std::max(0.0, bar_return_sq_sum_));
}

nlohmann::json RealizedVolatility::to_json() const {
    nlohmann::json j;
    j["tick_volatility"] = tick_volatility();
    j["tick_return_count"] = tick_returns_.size();
    j["time_bar_volatility"] = time_bar_volatility();
    j["time_bar_count"] = bar_returns_.size();
    return j;
}

}  // namespace hft
