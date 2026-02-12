#pragma once

/// @file instrument_registry.h
/// @brief Cold-path instrument configuration registry.
///
/// Maps InstrumentId <-> symbol string and stores per-instrument config.
/// Populated once at startup, read-only during operation.

#include <string>
#include <unordered_map>
#include <vector>

#include "core/types.h"

namespace hft {

/// Per-instrument configuration.
struct InstrumentConfig {
    InstrumentId instrument_id = DEFAULT_INSTRUMENT_ID;
    std::string symbol;          // e.g. "BTCUSDT", "ETHUSDT"
    Price min_price = 0;
    Price max_price = 0;
    Price tick_size = 0;
    size_t max_orders = 100000;
};

/// Cold-path registry mapping InstrumentId <-> symbol with per-instrument config.
class InstrumentRegistry {
public:
    InstrumentRegistry() = default;

    /// Register an instrument. Returns false if the id or symbol already exists.
    bool register_instrument(const InstrumentConfig& config);

    /// Look up config by instrument ID. Returns nullptr if not found.
    [[nodiscard]] const InstrumentConfig* find_by_id(InstrumentId id) const;

    /// Look up config by symbol string. Returns nullptr if not found.
    [[nodiscard]] const InstrumentConfig* find_by_symbol(const std::string& symbol) const;

    /// All registered instruments.
    [[nodiscard]] const std::vector<InstrumentConfig>& instruments() const { return instruments_; }

    /// Number of registered instruments.
    [[nodiscard]] size_t count() const { return instruments_.size(); }

private:
    std::vector<InstrumentConfig> instruments_;
    std::unordered_map<InstrumentId, size_t> id_to_index_;
    std::unordered_map<std::string, size_t> symbol_to_index_;
};

}  // namespace hft
