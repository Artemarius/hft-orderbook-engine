#pragma once

/// @file market_data_publisher.h
/// @brief Consumes EventMessages from the SPSC buffer and dispatches to
///        registered callbacks on a dedicated (cold-path) thread.
///
/// Cold-path component. Uses std::function and std::vector for callback
/// registration (called once at startup). The run() loop is designed for
/// a dedicated consumer thread.

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

#include "transport/event_buffer.h"
#include "transport/message.h"

namespace hft {

class MarketDataPublisher {
public:
    /// @param buffer SPSC ring buffer to consume events from.
    explicit MarketDataPublisher(EventBuffer& buffer) noexcept;

    MarketDataPublisher(const MarketDataPublisher&) = delete;
    MarketDataPublisher& operator=(const MarketDataPublisher&) = delete;

    /// Register a callback to be invoked for each event. Cold-path — call
    /// before run(). Not thread-safe with poll()/run().
    void register_callback(std::function<void(const EventMessage&)> callback);

    /// Non-blocking drain of all available events. Returns count processed.
    /// Invokes all registered callbacks for each event.
    [[nodiscard]] size_t poll() noexcept;

    /// Blocking event loop — calls poll() in a loop, yields when empty.
    /// Returns after stop() is called and remaining events are drained.
    void run();

    /// Thread-safe signal to exit the run() loop.
    void stop() noexcept;

    [[nodiscard]] uint64_t events_processed() const noexcept { return events_processed_; }
    [[nodiscard]] uint64_t last_sequence_num() const noexcept { return last_sequence_num_; }

private:
    EventBuffer& buffer_;
    std::vector<std::function<void(const EventMessage&)>> callbacks_;
    std::atomic<bool> running_;
    uint64_t events_processed_;
    uint64_t last_sequence_num_;
};

}  // namespace hft
