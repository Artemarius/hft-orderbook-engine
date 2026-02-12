#pragma once

/// @file analytics_config.h
/// @brief Configuration for the analytics engine and its modules.

#include <cstddef>
#include <cstdint>
#include <string>

namespace hft {

/// Configuration for all analytics modules.
struct AnalyticsConfig {
    size_t imbalance_window = 100;
    size_t vol_tick_window = 50;
    uint64_t vol_time_bar_ns = 1'000'000'000;  // 1 second
    size_t impact_regression_window = 200;
    size_t depth_max_levels = 10;
    std::string csv_path;   // empty = no CSV output
    std::string json_path;  // empty = no JSON output
};

}  // namespace hft
