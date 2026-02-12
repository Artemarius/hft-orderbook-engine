#pragma once

/// @file replay_engine.h
/// @brief L3 data replay orchestrator — replays CSV order data through the
///        matching engine and collects execution statistics.
///
/// Cold-path component. Owns the full processing pipeline: OrderBook,
/// MemoryPool, MatchingEngine, OrderGateway, EventBuffer, and
/// MarketDataPublisher. Reads an L3 CSV file via L3FeedParser and feeds
/// ADD/CANCEL events through the gateway.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/types.h"
#include "feed/l3_feed_parser.h"
#include "gateway/market_data_publisher.h"
#include "gateway/order_gateway.h"
#include "matching/matching_engine.h"
#include "orderbook/memory_pool.h"
#include "orderbook/order_book.h"
#include "transport/event_buffer.h"
#include "transport/message.h"

namespace hft {

/// Playback speed mode.
enum class PlaybackSpeed : uint8_t { Max, Realtime, FastForward };

/// Configuration for a replay session.
struct ReplayConfig {
    std::string input_path;
    std::string output_path;                         // JSON report (empty = none)
    PlaybackSpeed speed = PlaybackSpeed::Max;
    double speed_multiplier = 1.0;
    Price min_price  = 41000LL * PRICE_SCALE;        // $41,000
    Price max_price  = 43000LL * PRICE_SCALE;        // $43,000
    Price tick_size  = PRICE_SCALE / 100;            // $0.01
    size_t max_orders = 100000;
    bool enable_publisher = false;
    bool verbose = false;
};

/// Statistics collected during a replay session.
struct ReplayStats {
    uint64_t total_messages = 0;
    uint64_t add_messages = 0;
    uint64_t cancel_messages = 0;
    uint64_t trade_messages = 0;
    uint64_t parse_errors = 0;
    uint64_t orders_accepted = 0;
    uint64_t orders_rejected = 0;
    uint64_t orders_cancelled = 0;
    uint64_t trades_generated = 0;
    uint64_t cancel_failures = 0;
    Price final_best_bid = 0;
    Price final_best_ask = 0;
    Price final_spread = 0;
    size_t final_order_count = 0;
    double elapsed_seconds = 0.0;
    double messages_per_second = 0.0;
};

/// Orchestrates L3 data replay through the matching engine pipeline.
///
/// Usage:
///   ReplayConfig config;
///   config.input_path = "data/btcusdt_l3_sample.csv";
///   ReplayEngine engine(config);
///   ReplayStats stats = engine.run();
class ReplayEngine {
public:
    explicit ReplayEngine(const ReplayConfig& config);
    ~ReplayEngine();

    ReplayEngine(const ReplayEngine&) = delete;
    ReplayEngine& operator=(const ReplayEngine&) = delete;

    /// Run the replay to completion. Blocks until all records are processed.
    ReplayStats run();

    /// Register a callback to receive EventMessages during replay.
    /// Must be called before run().
    void register_event_callback(std::function<void(const EventMessage&)> cb);

    /// Access the order book (valid after run() completes).
    [[nodiscard]] const OrderBook& order_book() const { return *book_; }

private:
    /// Write a JSON report of the replay statistics.
    void write_report(const ReplayStats& stats) const;

    ReplayConfig config_;
    std::unique_ptr<OrderBook> book_;
    std::unique_ptr<MemoryPool<Order>> pool_;
    std::unique_ptr<MatchingEngine> engine_;
    std::unique_ptr<EventBuffer> event_buffer_;
    std::unique_ptr<OrderGateway> gateway_;
    std::unique_ptr<MarketDataPublisher> publisher_;
    std::vector<std::function<void(const EventMessage&)>> callbacks_;
};

}  // namespace hft
