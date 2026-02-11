#pragma once

/// @file types.h
/// @brief Core enums and type aliases for the matching engine hot path.
///
/// All types here are trivial, fixed-size, and free of heap allocation.
/// Price uses fixed-point int64_t to avoid floating-point comparison bugs.

#include <cstdint>

namespace hft {

enum class Side : uint8_t { Buy, Sell };

enum class OrderType : uint8_t {
    Limit,
    Market,
    IOC,    // Immediate-or-Cancel
    FOK,    // Fill-or-Kill
    GTC,    // Good-til-Cancelled (same as Limit with TIF=GTC)
    Iceberg
};

enum class TimeInForce : uint8_t {
    GTC,  // Good-til-Cancelled
    IOC,  // Immediate-or-Cancel
    FOK,  // Fill-or-Kill
    DAY   // Day order
};

enum class OrderStatus : uint8_t {
    New,
    Accepted,
    PartialFill,
    Filled,
    Cancelled,
    Rejected
};

/// Fixed-point price: actual_price * PRICE_SCALE.
/// int64_t avoids floating-point comparison bugs in price matching.
using Price = int64_t;

using Quantity = uint64_t;
using OrderId = uint64_t;
using ParticipantId = uint32_t;

/// Nanoseconds since epoch (or rdtsc ticks on the hot path).
using Timestamp = uint64_t;

/// 10^8 — supports 8 decimal places (sufficient for crypto).
constexpr int64_t PRICE_SCALE = 100'000'000;

}  // namespace hft
