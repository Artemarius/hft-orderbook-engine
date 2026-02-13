#include "analytics/depth_profile.h"

#include <vector>

#include <nlohmann/json.hpp>

namespace hft {

DepthProfile::DepthProfile(size_t max_levels)
    : max_levels_(max_levels),
      avg_bid_depth_(max_levels, 0.0),
      avg_ask_depth_(max_levels, 0.0) {}

void DepthProfile::on_event(const EventMessage& /*event*/,
                             const OrderBook& book) {
    // Allocate stack buffer for depth entries
    std::vector<DepthEntry> bid_entries(max_levels_);
    std::vector<DepthEntry> ask_entries(max_levels_);

    size_t bid_count = book.get_bid_depth(bid_entries.data(), max_levels_);
    size_t ask_count = book.get_ask_depth(ask_entries.data(), max_levels_);

    // Store current depth
    bid_depth_.resize(bid_count);
    for (size_t i = 0; i < bid_count; ++i) {
        bid_depth_[i] = bid_entries[i].quantity;
    }

    ask_depth_.resize(ask_count);
    for (size_t i = 0; i < ask_count; ++i) {
        ask_depth_[i] = ask_entries[i].quantity;
    }

    // Update running average depth
    ++snapshot_count_;
    auto n = static_cast<double>(snapshot_count_);

    for (size_t i = 0; i < max_levels_; ++i) {
        double bid_qty = (i < bid_count) ? static_cast<double>(bid_entries[i].quantity) : 0.0;
        double ask_qty = (i < ask_count) ? static_cast<double>(ask_entries[i].quantity) : 0.0;

        // Incremental mean: avg = avg + (x - avg) / n
        avg_bid_depth_[i] += (bid_qty - avg_bid_depth_[i]) / n;
        avg_ask_depth_[i] += (ask_qty - avg_ask_depth_[i]) / n;
    }
}

double DepthProfile::depth_imbalance() const {
    double sum_bid = 0.0;
    double sum_ask = 0.0;

    for (auto q : bid_depth_) sum_bid += static_cast<double>(q);
    for (auto q : ask_depth_) sum_ask += static_cast<double>(q);

    double total = sum_bid + sum_ask;
    if (total <= 0.0) return 0.0;
    return (sum_bid - sum_ask) / total;
}

nlohmann::json DepthProfile::to_json() const {
    nlohmann::json j;
    j["depth_imbalance"] = depth_imbalance();
    j["max_levels"] = max_levels_;
    j["bid_levels_filled"] = bid_depth_.size();
    j["ask_levels_filled"] = ask_depth_.size();

    // Current depth
    j["current_bid_depth"] = nlohmann::json::array();
    for (auto q : bid_depth_) j["current_bid_depth"].push_back(q);

    j["current_ask_depth"] = nlohmann::json::array();
    for (auto q : ask_depth_) j["current_ask_depth"].push_back(q);

    // Average depth
    j["avg_bid_depth"] = nlohmann::json::array();
    for (size_t i = 0; i < max_levels_; ++i) {
        j["avg_bid_depth"].push_back(avg_bid_depth_[i]);
    }

    j["avg_ask_depth"] = nlohmann::json::array();
    for (size_t i = 0; i < max_levels_; ++i) {
        j["avg_ask_depth"].push_back(avg_ask_depth_[i]);
    }

    j["snapshot_count"] = snapshot_count_;
    return j;
}

}  // namespace hft
