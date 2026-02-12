#pragma once

/// @file multi_instrument_analytics.h
/// @brief Per-instrument analytics orchestrator.
///
/// Manages one AnalyticsEngine per instrument, routing events by instrument_id.

#include <memory>
#include <string>
#include <unordered_map>

#include "analytics/analytics_config.h"
#include "analytics/analytics_engine.h"
#include "core/types.h"
#include "gateway/instrument_router.h"
#include "transport/message.h"

namespace hft {

/// Manages per-instrument AnalyticsEngine instances.
class MultiInstrumentAnalytics {
public:
    /// @param router Reference to the instrument router (must outlive this).
    /// @param config Shared analytics configuration for all instruments.
    explicit MultiInstrumentAnalytics(const InstrumentRouter& router,
                                       const AnalyticsConfig& config = {});

    /// Route an event to the correct per-instrument analytics engine.
    void on_event(const EventMessage& event);

    /// Access per-instrument analytics. Returns nullptr if unknown id.
    [[nodiscard]] const AnalyticsEngine* analytics(InstrumentId id) const;

    /// Write aggregate JSON for all instruments.
    void write_json(const std::string& path) const;

    /// Print per-instrument summary to stdout.
    void print_summary() const;

private:
    std::unordered_map<InstrumentId, std::unique_ptr<AnalyticsEngine>> engines_;
};

}  // namespace hft
