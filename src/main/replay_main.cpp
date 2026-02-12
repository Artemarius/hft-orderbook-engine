/// @file replay_main.cpp
/// @brief CLI executable for replaying L3 market data through the matching engine.
///
/// Usage:
///   ./replay --input data/btcusdt_l3_sample.csv [--output report.json]
///            [--speed max|realtime|2x] [--verbose]
///            [--analytics] [--analytics-json <path>] [--analytics-csv <path>]
///
/// Automatically detects multi-instrument CSV files (7-column format with
/// "symbol" header) and uses MultiInstrumentReplayEngine.

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "analytics/analytics_engine.h"
#include "analytics/multi_instrument_analytics.h"
#include "core/types.h"
#include "feed/multi_instrument_replay_engine.h"
#include "feed/replay_engine.h"

using namespace hft;

static void print_usage(const char* program) {
    std::cerr
        << "Usage: " << program << " --input <file.csv> [options]\n"
        << "\n"
        << "Options:\n"
        << "  --input  <path>          Input L3 CSV file (required)\n"
        << "  --output <path>          Output JSON report file\n"
        << "  --speed  <mode>          Playback speed: max (default), realtime, <N>x\n"
        << "  --verbose                Print detailed progress\n"
        << "  --analytics              Enable analytics and print summary\n"
        << "  --analytics-json <path>  Write analytics JSON to file\n"
        << "  --analytics-csv  <path>  Write analytics time-series CSV to file\n"
        << "  --help                   Show this help message\n";
}

static void print_price(const char* label, Price price) {
    double value = static_cast<double>(price) / static_cast<double>(PRICE_SCALE);
    std::cout << "  " << label << ": $" << value << "\n";
}

/// Detect if a CSV file is multi-instrument (7-column with "symbol" header).
static bool is_multi_instrument_csv(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string line;
    if (!std::getline(file, line)) return false;

    // Trim trailing whitespace
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' ||
                             line.back() == ' ')) {
        line.pop_back();
    }

    // Check if first field is "symbol" (case-insensitive)
    if (line.size() < 6) return false;
    std::string prefix = line.substr(0, 6);
    for (auto& c : prefix) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return prefix == "symbol";
}

int main(int argc, char* argv[]) {
    ReplayConfig config;
    bool enable_analytics = false;
    std::string analytics_json_path;
    std::string analytics_csv_path;

    // Hand-rolled argument parsing
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (std::strcmp(argv[i], "--input") == 0) {
            if (++i >= argc) {
                std::cerr << "Error: --input requires a path argument\n";
                return 1;
            }
            config.input_path = argv[i];
        } else if (std::strcmp(argv[i], "--output") == 0) {
            if (++i >= argc) {
                std::cerr << "Error: --output requires a path argument\n";
                return 1;
            }
            config.output_path = argv[i];
        } else if (std::strcmp(argv[i], "--speed") == 0) {
            if (++i >= argc) {
                std::cerr << "Error: --speed requires a mode argument\n";
                return 1;
            }
            std::string mode = argv[i];
            if (mode == "max") {
                config.speed = PlaybackSpeed::Max;
            } else if (mode == "realtime") {
                config.speed = PlaybackSpeed::Realtime;
                config.speed_multiplier = 1.0;
            } else if (mode.size() > 1 && mode.back() == 'x') {
                config.speed = PlaybackSpeed::FastForward;
                config.speed_multiplier = std::atof(mode.c_str());
                if (config.speed_multiplier <= 0.0) {
                    std::cerr << "Error: invalid speed multiplier: " << mode << "\n";
                    return 1;
                }
            } else {
                std::cerr << "Error: unknown speed mode: " << mode << "\n";
                return 1;
            }
        } else if (std::strcmp(argv[i], "--verbose") == 0) {
            config.verbose = true;
        } else if (std::strcmp(argv[i], "--analytics") == 0) {
            enable_analytics = true;
        } else if (std::strcmp(argv[i], "--analytics-json") == 0) {
            if (++i >= argc) {
                std::cerr << "Error: --analytics-json requires a path argument\n";
                return 1;
            }
            analytics_json_path = argv[i];
            enable_analytics = true;
        } else if (std::strcmp(argv[i], "--analytics-csv") == 0) {
            if (++i >= argc) {
                std::cerr << "Error: --analytics-csv requires a path argument\n";
                return 1;
            }
            analytics_csv_path = argv[i];
            enable_analytics = true;
        } else {
            std::cerr << "Error: unknown option: " << argv[i] << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (config.input_path.empty()) {
        std::cerr << "Error: --input is required\n";
        print_usage(argv[0]);
        return 1;
    }

    // Detect multi-instrument CSV
    bool multi_instrument = is_multi_instrument_csv(config.input_path);

    if (multi_instrument) {
        // --- Multi-instrument path ---
        std::cout << "Replaying (multi-instrument): " << config.input_path << "\n";

        MultiReplayConfig multi_config;
        multi_config.input_path = config.input_path;
        multi_config.output_path = config.output_path;
        multi_config.auto_discover = true;
        multi_config.verbose = config.verbose;

        MultiInstrumentReplayEngine engine(multi_config);

        std::unique_ptr<MultiInstrumentAnalytics> analytics;
        if (enable_analytics) {
            // Analytics is set up after run() for auto-discover mode.
            // Register a deferred callback that buffers events.
            std::vector<EventMessage> buffered_events;
            engine.register_event_callback(
                [&buffered_events](const EventMessage& event) {
                    buffered_events.push_back(event);
                });

            MultiReplayStats stats = engine.run();

            if (stats.total_messages == 0) {
                std::cerr << "No messages processed. Check input file path.\n";
                return 1;
            }

            // Create analytics post-run and replay buffered events
            analytics = std::make_unique<MultiInstrumentAnalytics>(engine.router());
            for (const auto& event : buffered_events) {
                analytics->on_event(event);
            }

            // Print summary
            std::cout << "\n=== Multi-Instrument Replay Summary ===\n";
            std::cout << "Total messages: " << stats.total_messages << "\n";
            std::cout << "Instruments:    " << stats.per_instrument.size() << "\n";

            for (const auto& ps : stats.per_instrument) {
                std::cout << "\n--- " << ps.symbol << " (id=" << ps.instrument_id << ") ---\n";
                std::cout << "  ADD: " << ps.add_messages
                          << "  CANCEL: " << ps.cancel_messages
                          << "  MODIFY: " << ps.modify_messages << "\n";
                std::cout << "  Accepted: " << ps.orders_accepted
                          << "  Rejected: " << ps.orders_rejected << "\n";
                std::cout << "  Trades generated: " << ps.trades_generated << "\n";
                std::cout << "  Final orders: " << ps.final_order_count << "\n";
                print_price("Best bid", ps.final_best_bid);
                print_price("Best ask", ps.final_best_ask);
            }

            std::cout << "\nPerformance:\n";
            std::cout << "  Elapsed:    " << stats.elapsed_seconds << " s\n";
            std::cout << "  Throughput: " << stats.messages_per_second << " msgs/s\n";

            analytics->print_summary();

            if (!analytics_json_path.empty()) {
                analytics->write_json(analytics_json_path);
                std::cout << "\nAnalytics JSON written to: " << analytics_json_path << "\n";
            }
        } else {
            MultiReplayStats stats = engine.run();

            if (stats.total_messages == 0) {
                std::cerr << "No messages processed. Check input file path.\n";
                return 1;
            }

            std::cout << "\n=== Multi-Instrument Replay Summary ===\n";
            std::cout << "Total messages: " << stats.total_messages << "\n";
            std::cout << "Instruments:    " << stats.per_instrument.size() << "\n";

            for (const auto& ps : stats.per_instrument) {
                std::cout << "\n--- " << ps.symbol << " (id=" << ps.instrument_id << ") ---\n";
                std::cout << "  ADD: " << ps.add_messages
                          << "  CANCEL: " << ps.cancel_messages
                          << "  MODIFY: " << ps.modify_messages << "\n";
                std::cout << "  Accepted: " << ps.orders_accepted
                          << "  Rejected: " << ps.orders_rejected << "\n";
                std::cout << "  Trades generated: " << ps.trades_generated << "\n";
                std::cout << "  Final orders: " << ps.final_order_count << "\n";
                print_price("Best bid", ps.final_best_bid);
                print_price("Best ask", ps.final_best_ask);
            }

            std::cout << "\nPerformance:\n";
            std::cout << "  Elapsed:    " << stats.elapsed_seconds << " s\n";
            std::cout << "  Throughput: " << stats.messages_per_second << " msgs/s\n";
        }
    } else {
        // --- Single-instrument path (original) ---

        // Analytics requires the publisher to generate events
        if (enable_analytics) {
            config.enable_publisher = true;
        }

        std::cout << "Replaying: " << config.input_path << "\n";

        ReplayEngine engine(config);

        std::unique_ptr<AnalyticsEngine> analytics;
        if (enable_analytics) {
            analytics = std::make_unique<AnalyticsEngine>(engine.order_book());
            engine.register_event_callback(
                [&analytics](const EventMessage& event) {
                    analytics->on_event(event);
                });
        }

        ReplayStats stats = engine.run();

        if (stats.total_messages == 0) {
            std::cerr << "No messages processed. Check input file path.\n";
            return 1;
        }

        std::cout << "\n=== Replay Summary ===\n";
        std::cout << "Messages:\n";
        std::cout << "  Total:        " << stats.total_messages << "\n";
        std::cout << "  ADD:          " << stats.add_messages << "\n";
        std::cout << "  CANCEL:       " << stats.cancel_messages << "\n";
        std::cout << "  TRADE (info): " << stats.trade_messages << "\n";
        std::cout << "  Parse errors: " << stats.parse_errors << "\n";

        std::cout << "\nOrders:\n";
        std::cout << "  Accepted:       " << stats.orders_accepted << "\n";
        std::cout << "  Rejected:       " << stats.orders_rejected << "\n";
        std::cout << "  Cancelled:      " << stats.orders_cancelled << "\n";
        std::cout << "  Cancel failures: " << stats.cancel_failures << "\n";

        std::cout << "\nTrades:\n";
        std::cout << "  Generated: " << stats.trades_generated << "\n";

        std::cout << "\nFinal book state:\n";
        std::cout << "  Orders:  " << stats.final_order_count << "\n";
        print_price("Best bid", stats.final_best_bid);
        print_price("Best ask", stats.final_best_ask);
        print_price("Spread ", stats.final_spread);

        std::cout << "\nPerformance:\n";
        std::cout << "  Elapsed:  " << stats.elapsed_seconds << " s\n";
        std::cout << "  Throughput: " << stats.messages_per_second << " msgs/s\n";

        if (!config.output_path.empty()) {
            std::cout << "\nReport written to: " << config.output_path << "\n";
        }

        if (analytics) {
            analytics->print_summary();

            if (!analytics_json_path.empty()) {
                analytics->write_json(analytics_json_path);
                std::cout << "\nAnalytics JSON written to: " << analytics_json_path << "\n";
            }
            if (!analytics_csv_path.empty()) {
                analytics->write_csv(analytics_csv_path);
                std::cout << "Analytics CSV written to: " << analytics_csv_path << "\n";
            }
        }
    }

    return 0;
}
