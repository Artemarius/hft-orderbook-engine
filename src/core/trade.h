#pragma once

/// @file trade.h
/// @brief Trade struct — POD record of a matched trade on the hot path.

#include <type_traits>

#include "core/types.h"

namespace hft {

struct Trade {
    uint64_t trade_id;
    OrderId buy_order_id;
    OrderId sell_order_id;
    Price price;
    Quantity quantity;
    Timestamp timestamp;
};

static_assert(std::is_trivially_copyable_v<Trade>,
              "Trade must be trivially copyable for hot-path use");
static_assert(std::is_standard_layout_v<Trade>,
              "Trade must be standard layout");
static_assert(sizeof(Trade) == 48,
              "Trade should be exactly 48 bytes (6 x 8-byte fields)");

}  // namespace hft
