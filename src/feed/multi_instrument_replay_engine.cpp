#include "feed/multi_instrument_replay_engine.h"

#include <chrono>
#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>

#include "orderbook/order_book.h"

namespace hft {

MultiInstrumentReplayEngine::MultiInstrumentReplayEngine(
    const MultiReplayConfig& config)
    : config_(config) {
    // Register pre-configured instruments
    for (const auto& cfg : config_.instruments) {
        registry_.register_instrument(cfg);
    }

    // Create shared event buffer and router
    event_buffer_ = std::make_unique<EventBuffer>();

    if (!config_.auto_discover) {
        router_ = std::make_unique<InstrumentRouter>(registry_, event_buffer_.get());
    }
    // If auto_discover, router is created lazily after first pass discovers symbols
}

MultiInstrumentReplayEngine::~MultiInstrumentReplayEngine() = default;

void MultiInstrumentReplayEngine::register_event_callback(
    std::function<void(const EventMessage&)> cb) {
    callbacks_.push_back(std::move(cb));
}

MultiReplayStats MultiInstrumentReplayEngine::run() {
    MultiReplayStats stats{};

    L3FeedParser parser;
    if (!parser.open(config_.input_path)) {
        std::cerr << "Failed to open input file: " << config_.input_path << "\n";
        return stats;
    }

    // Auto-discovery pass: read through to find all symbols, then rewind
    if (config_.auto_discover && !router_) {
        L3Record record;
        while (parser.next(record)) {
            if (!record.valid) continue;
            if (!record.symbol.empty() &&
                auto_symbol_map_.find(record.symbol) == auto_symbol_map_.end()) {
                InstrumentId id = next_auto_id_++;
                auto_symbol_map_[record.symbol] = id;

                InstrumentConfig cfg;
                cfg.instrument_id = id;
                cfg.symbol = record.symbol;
                cfg.min_price = config_.default_min_price;
                cfg.max_price = config_.default_max_price;
                cfg.tick_size = config_.default_tick_size;
                cfg.max_orders = config_.default_max_orders;
                registry_.register_instrument(cfg);
            }
        }
        parser.reset();

        if (registry_.count() == 0) {
            std::cerr << "No instruments discovered in file\n";
            return stats;
        }

        router_ = std::make_unique<InstrumentRouter>(registry_, event_buffer_.get());
    }

    // Set up publisher and callbacks
    publisher_ = std::make_unique<MarketDataPublisher>(*event_buffer_);
    for (auto& cb : callbacks_) {
        publisher_->register_callback(cb);
    }

    // Build symbol -> InstrumentId map for routing
    std::unordered_map<std::string, InstrumentId> symbol_map;
    for (const auto& cfg : registry_.instruments()) {
        symbol_map[cfg.symbol] = cfg.instrument_id;
    }

    // Initialize per-instrument stats
    std::unordered_map<InstrumentId, size_t> id_to_stat_index;
    for (const auto& cfg : registry_.instruments()) {
        size_t idx = stats.per_instrument.size();
        PerInstrumentStats ps;
        ps.instrument_id = cfg.instrument_id;
        ps.symbol = cfg.symbol;
        stats.per_instrument.push_back(ps);
        id_to_stat_index[cfg.instrument_id] = idx;
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

        // Resolve instrument_id from symbol
        InstrumentId inst_id = DEFAULT_INSTRUMENT_ID;
        if (!record.symbol.empty()) {
            auto it = symbol_map.find(record.symbol);
            if (it == symbol_map.end()) {
                if (config_.verbose) {
                    std::cerr << "Unknown symbol: " << record.symbol << "\n";
                }
                continue;
            }
            inst_id = it->second;
        }

        auto stat_it = id_to_stat_index.find(inst_id);
        if (stat_it == id_to_stat_index.end()) continue;
        PerInstrumentStats& ps = stats.per_instrument[stat_it->second];

        switch (record.event_type) {
            case L3EventType::Add: {
                ++ps.add_messages;
                OrderMessage msg = L3FeedParser::to_order_message(record, inst_id);
                GatewayResult result = router_->process_order(msg);
                if (result.accepted) {
                    ++ps.orders_accepted;
                    ps.trades_generated += result.trade_count;
                } else {
                    ++ps.orders_rejected;
                }
                break;
            }

            case L3EventType::Cancel: {
                ++ps.cancel_messages;
                bool cancelled = router_->process_cancel(inst_id, record.order_id);
                if (cancelled) {
                    ++ps.orders_cancelled;
                } else {
                    ++ps.cancel_failures;
                }
                break;
            }

            case L3EventType::Modify: {
                ++ps.modify_messages;
                OrderMessage msg = L3FeedParser::to_modify_message(record, inst_id);
                GatewayResult result = router_->process_modify(msg);
                if (result.accepted) {
                    ++ps.orders_modified;
                    ps.trades_generated += result.trade_count;
                } else {
                    ++ps.modify_failures;
                }
                break;
            }

            case L3EventType::Trade:
                ++ps.trade_messages;
                break;

            case L3EventType::Invalid:
                ++stats.parse_errors;
                break;
        }

        // Drain publisher events
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

    stats.parse_errors = parser.parse_errors();
    stats.elapsed_seconds = elapsed.count();
    stats.messages_per_second =
        (stats.elapsed_seconds > 0.0)
            ? static_cast<double>(stats.total_messages) / stats.elapsed_seconds
            : 0.0;

    // Collect final per-instrument book state
    for (auto& ps : stats.per_instrument) {
        const OrderBook* book = router_->order_book(ps.instrument_id);
        if (book) {
            ps.final_order_count = book->order_count();
            const PriceLevel* bid = book->best_bid();
            const PriceLevel* ask = book->best_ask();
            ps.final_best_bid = bid ? bid->price : 0;
            ps.final_best_ask = ask ? ask->price : 0;
        }
    }

    parser.close();

    if (!config_.output_path.empty()) {
        write_report(stats);
    }

    return stats;
}

void MultiInstrumentReplayEngine::write_report(
    const MultiReplayStats& stats) const {
    nlohmann::json report;

    report["input_file"] = config_.input_path;
    report["total_messages"] = stats.total_messages;
    report["parse_errors"] = stats.parse_errors;
    report["elapsed_seconds"] = stats.elapsed_seconds;
    report["messages_per_second"] = stats.messages_per_second;
    report["instrument_count"] = stats.per_instrument.size();

    auto price_to_double = [](Price p) -> double {
        return static_cast<double>(p) / static_cast<double>(PRICE_SCALE);
    };

    nlohmann::json instruments = nlohmann::json::array();
    for (const auto& ps : stats.per_instrument) {
        nlohmann::json inst;
        inst["instrument_id"] = ps.instrument_id;
        inst["symbol"] = ps.symbol;
        inst["messages"]["add"] = ps.add_messages;
        inst["messages"]["cancel"] = ps.cancel_messages;
        inst["messages"]["modify"] = ps.modify_messages;
        inst["messages"]["trade"] = ps.trade_messages;
        inst["orders"]["accepted"] = ps.orders_accepted;
        inst["orders"]["rejected"] = ps.orders_rejected;
        inst["orders"]["cancelled"] = ps.orders_cancelled;
        inst["orders"]["modified"] = ps.orders_modified;
        inst["trades"]["generated"] = ps.trades_generated;
        inst["final_state"]["order_count"] = ps.final_order_count;
        inst["final_state"]["best_bid"] = price_to_double(ps.final_best_bid);
        inst["final_state"]["best_ask"] = price_to_double(ps.final_best_ask);
        instruments.push_back(inst);
    }
    report["instruments"] = instruments;

    std::ofstream out(config_.output_path);
    if (out.is_open()) {
        out << report.dump(2) << "\n";
    } else {
        std::cerr << "Failed to write report to: " << config_.output_path << "\n";
    }
}

}  // namespace hft
