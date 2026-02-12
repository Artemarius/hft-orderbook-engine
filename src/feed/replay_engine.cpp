#include "feed/replay_engine.h"

#include <chrono>
#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>

namespace hft {

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

ReplayEngine::ReplayEngine(const ReplayConfig& config)
    : config_(config) {
    // Allocate pipeline components
    book_ = std::make_unique<OrderBook>(
        config_.min_price, config_.max_price, config_.tick_size, config_.max_orders);
    pool_ = std::make_unique<MemoryPool<Order>>(config_.max_orders);

    engine_ = std::make_unique<MatchingEngine>(
        *book_, *pool_, SelfTradePreventionMode::None);

    if (config_.enable_publisher) {
        event_buffer_ = std::make_unique<EventBuffer>();
        gateway_ = std::make_unique<OrderGateway>(*engine_, *pool_, event_buffer_.get());
        publisher_ = std::make_unique<MarketDataPublisher>(*event_buffer_);
    } else {
        gateway_ = std::make_unique<OrderGateway>(*engine_, *pool_, nullptr);
    }
}

ReplayEngine::~ReplayEngine() = default;

// ---------------------------------------------------------------------------
// Event callback registration
// ---------------------------------------------------------------------------

void ReplayEngine::register_event_callback(
    std::function<void(const EventMessage&)> cb) {
    callbacks_.push_back(std::move(cb));
}

// ---------------------------------------------------------------------------
// Replay execution
// ---------------------------------------------------------------------------

ReplayStats ReplayEngine::run() {
    ReplayStats stats{};

    // Register callbacks with the publisher
    if (publisher_) {
        for (auto& cb : callbacks_) {
            publisher_->register_callback(cb);
        }
    }

    L3FeedParser parser;
    if (!parser.open(config_.input_path)) {
        std::cerr << "Failed to open input file: " << config_.input_path << "\n";
        return stats;
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    L3Record record;
    while (parser.next(record)) {
        ++stats.total_messages;

        if (!record.valid) {
            ++stats.parse_errors;
            if (config_.verbose) {
                std::cerr << "Parse error at line " << parser.lines_read()
                          << ": " << record.error << "\n";
            }
            continue;
        }

        switch (record.event_type) {
            case L3EventType::Add: {
                ++stats.add_messages;
                OrderMessage msg = L3FeedParser::to_order_message(record);
                GatewayResult result = gateway_->process_order(msg);
                if (result.accepted) {
                    ++stats.orders_accepted;
                    stats.trades_generated += result.trade_count;
                } else {
                    ++stats.orders_rejected;
                    if (config_.verbose) {
                        std::cerr << "Order " << record.order_id
                                  << " rejected (reason "
                                  << static_cast<int>(result.reject_reason)
                                  << ")\n";
                    }
                }
                break;
            }

            case L3EventType::Cancel: {
                ++stats.cancel_messages;
                bool cancelled = gateway_->process_cancel(record.order_id);
                if (cancelled) {
                    ++stats.orders_cancelled;
                } else {
                    ++stats.cancel_failures;
                }
                break;
            }

            case L3EventType::Trade: {
                ++stats.trade_messages;
                // TRADE records are informational only — matching engine
                // generates its own trades from ADD events
                break;
            }

            case L3EventType::Invalid:
                ++stats.parse_errors;
                break;
        }

        // Drain publisher events if enabled
        if (publisher_) {
            (void)publisher_->poll();
        }
    }

    // Final drain
    if (publisher_) {
        (void)publisher_->poll();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;

    // Collect final state
    stats.parse_errors = parser.parse_errors();
    stats.final_order_count = book_->order_count();
    stats.elapsed_seconds = elapsed.count();
    stats.messages_per_second =
        (stats.elapsed_seconds > 0.0)
            ? static_cast<double>(stats.total_messages) / stats.elapsed_seconds
            : 0.0;

    const PriceLevel* best_bid = book_->best_bid();
    const PriceLevel* best_ask = book_->best_ask();
    stats.final_best_bid = best_bid ? best_bid->price : 0;
    stats.final_best_ask = best_ask ? best_ask->price : 0;
    stats.final_spread = book_->spread();

    parser.close();

    // Write JSON report if output path specified
    if (!config_.output_path.empty()) {
        write_report(stats);
    }

    return stats;
}

// ---------------------------------------------------------------------------
// JSON report
// ---------------------------------------------------------------------------

void ReplayEngine::write_report(const ReplayStats& stats) const {
    nlohmann::json report;

    report["input_file"] = config_.input_path;
    report["messages"]["total"] = stats.total_messages;
    report["messages"]["add"] = stats.add_messages;
    report["messages"]["cancel"] = stats.cancel_messages;
    report["messages"]["trade"] = stats.trade_messages;
    report["messages"]["parse_errors"] = stats.parse_errors;

    report["orders"]["accepted"] = stats.orders_accepted;
    report["orders"]["rejected"] = stats.orders_rejected;
    report["orders"]["cancelled"] = stats.orders_cancelled;
    report["orders"]["cancel_failures"] = stats.cancel_failures;

    report["trades"]["generated"] = stats.trades_generated;

    // Convert fixed-point prices to human-readable doubles for the report
    auto price_to_double = [](Price p) -> double {
        return static_cast<double>(p) / static_cast<double>(PRICE_SCALE);
    };

    report["final_state"]["order_count"] = stats.final_order_count;
    report["final_state"]["best_bid"] = price_to_double(stats.final_best_bid);
    report["final_state"]["best_ask"] = price_to_double(stats.final_best_ask);
    report["final_state"]["spread"] = price_to_double(stats.final_spread);

    report["performance"]["elapsed_seconds"] = stats.elapsed_seconds;
    report["performance"]["messages_per_second"] = stats.messages_per_second;

    std::ofstream out(config_.output_path);
    if (out.is_open()) {
        out << report.dump(2) << "\n";
    } else {
        std::cerr << "Failed to write report to: " << config_.output_path << "\n";
    }
}

}  // namespace hft
