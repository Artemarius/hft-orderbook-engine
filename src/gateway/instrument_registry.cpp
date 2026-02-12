#include "gateway/instrument_registry.h"

namespace hft {

bool InstrumentRegistry::register_instrument(const InstrumentConfig& config) {
    // Reject duplicate id or symbol
    if (id_to_index_.count(config.instrument_id) > 0) return false;
    if (symbol_to_index_.count(config.symbol) > 0) return false;

    size_t index = instruments_.size();
    instruments_.push_back(config);
    id_to_index_[config.instrument_id] = index;
    symbol_to_index_[config.symbol] = index;
    return true;
}

const InstrumentConfig* InstrumentRegistry::find_by_id(InstrumentId id) const {
    auto it = id_to_index_.find(id);
    if (it == id_to_index_.end()) return nullptr;
    return &instruments_[it->second];
}

const InstrumentConfig* InstrumentRegistry::find_by_symbol(
    const std::string& symbol) const {
    auto it = symbol_to_index_.find(symbol);
    if (it == symbol_to_index_.end()) return nullptr;
    return &instruments_[it->second];
}

}  // namespace hft
