#include "analytics/multi_instrument_analytics.h"

#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>

namespace hft {

MultiInstrumentAnalytics::MultiInstrumentAnalytics(
    const InstrumentRouter& router, const AnalyticsConfig& config) {
    for (size_t i = 0; i < router.instrument_count(); ++i) {
        // Iterate via pipeline accessor — try sequential IDs
        // We need to find all valid instrument IDs. Since the router uses
        // a flat array, iterate possible IDs up to a reasonable bound.
    }

    // Alternative: use a brute-force scan of reasonable IDs.
    // The router stores pipelines in a vector; we check IDs 0..max.
    for (InstrumentId id = 0; engines_.size() < router.instrument_count(); ++id) {
        const OrderBook* book = router.order_book(id);
        if (book) {
            engines_[id] = std::make_unique<AnalyticsEngine>(*book, config);
        }
        if (id == UINT32_MAX) break;  // prevent infinite loop
    }
}

void MultiInstrumentAnalytics::on_event(const EventMessage& event) {
    auto it = engines_.find(event.instrument_id);
    if (it != engines_.end()) {
        it->second->on_event(event);
    }
}

const AnalyticsEngine* MultiInstrumentAnalytics::analytics(
    InstrumentId id) const {
    auto it = engines_.find(id);
    return (it != engines_.end()) ? it->second.get() : nullptr;
}

void MultiInstrumentAnalytics::write_json(const std::string& path) const {
    nlohmann::json report;

    for (const auto& [id, engine] : engines_) {
        report["instruments"][std::to_string(id)] = engine->to_json();
    }

    std::ofstream out(path);
    if (out.is_open()) {
        out << report.dump(2) << "\n";
    } else {
        std::cerr << "Failed to write analytics JSON to: " << path << "\n";
    }
}

void MultiInstrumentAnalytics::print_summary() const {
    for (const auto& [id, engine] : engines_) {
        std::cout << "\n--- Instrument " << id << " ---";
        engine->print_summary();
    }
}

}  // namespace hft
