#pragma once

/// @file multi_instrument_replay_engine.h
/// @brief Multi-instrument L3 data replay orchestrator.
///
/// Reads a 7-column CSV (symbol,timestamp,event_type,...), routes each
/// message to the correct per-instrument pipeline via InstrumentRouter,
/// and collects per-instrument statistics.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/types.h"
#include "feed/l3_feed_parser.h"
#include "gateway/instrument_registry.h"
#include "gateway/instrument_router.h"
#include "gateway/market_data_publisher.h"
#include "transport/event_buffer.h"
#include "transport/message.h"

namespace hft {

/// Configuration for a multi-instrument replay session.
struct MultiReplayConfig {
    std::string input_path;
    std::string output_path;
    std::vector<InstrumentConfig> instruments;
    bool auto_discover = false;   // discover symbols from CSV on-the-fly
    Price default_min_price  = 1 * PRICE_SCALE;
    Price default_max_price  = 100000LL * PRICE_SCALE;
    Price default_tick_size  = PRICE_SCALE / 100;
    size_t default_max_orders = 100000;
    bool verbose = false;
};

/// Per-instrument statistics collected during replay.
struct PerInstrumentStats {
    InstrumentId instrument_id = DEFAULT_INSTRUMENT_ID;
    std::string symbol;
    uint64_t add_messages = 0;
    uint64_t cancel_messages = 0;
    uint64_t modify_messages = 0;
    uint64_t trade_messages = 0;
    uint64_t orders_accepted = 0;
    uint64_t orders_rejected = 0;
    uint64_t orders_cancelled = 0;
    uint64_t cancel_failures = 0;
    uint64_t orders_modified = 0;
    uint64_t modify_failures = 0;
    uint64_t trades_generated = 0;
    Price final_best_bid = 0;
    Price final_best_ask = 0;
    size_t final_order_count = 0;
};

/// Aggregate statistics across all instruments.
struct MultiReplayStats {
    uint64_t total_messages = 0;
    uint64_t parse_errors = 0;
    double elapsed_seconds = 0.0;
    double messages_per_second = 0.0;
    std::vector<PerInstrumentStats> per_instrument;
};

/// Multi-instrument replay engine.
class MultiInstrumentReplayEngine {
public:
    explicit MultiInstrumentReplayEngine(const MultiReplayConfig& config);
    ~MultiInstrumentReplayEngine();

    MultiInstrumentReplayEngine(const MultiInstrumentReplayEngine&) = delete;
    MultiInstrumentReplayEngine& operator=(const MultiInstrumentReplayEngine&) = delete;

    /// Run replay to completion.
    MultiReplayStats run();

    /// Register a callback to receive EventMessages during replay.
    void register_event_callback(std::function<void(const EventMessage&)> cb);

    /// Access the router (valid after construction).
    [[nodiscard]] const InstrumentRouter& router() const { return *router_; }

    /// Access the registry.
    [[nodiscard]] const InstrumentRegistry& registry() const { return registry_; }

private:
    void write_report(const MultiReplayStats& stats) const;

    MultiReplayConfig config_;
    InstrumentRegistry registry_;
    std::unique_ptr<EventBuffer> event_buffer_;
    std::unique_ptr<InstrumentRouter> router_;
    std::unique_ptr<MarketDataPublisher> publisher_;
    std::vector<std::function<void(const EventMessage&)>> callbacks_;

    // Auto-discovery state
    InstrumentId next_auto_id_ = 0;
    std::unordered_map<std::string, InstrumentId> auto_symbol_map_;
};

}  // namespace hft
