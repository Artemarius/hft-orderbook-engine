#include "gateway/market_data_publisher.h"

#include <thread>

namespace hft {

MarketDataPublisher::MarketDataPublisher(EventBuffer& buffer) noexcept
    : buffer_(buffer),
      running_(false),
      events_processed_(0),
      last_sequence_num_(0) {}

void MarketDataPublisher::register_callback(
    std::function<void(const EventMessage&)> callback) {
    callbacks_.push_back(std::move(callback));
}

size_t MarketDataPublisher::poll() noexcept {
    size_t count = 0;
    EventMessage event{};

    while (buffer_.try_pop(event)) {
        for (auto& cb : callbacks_) {
            cb(event);
        }
        last_sequence_num_ = event.sequence_num;
        ++events_processed_;
        ++count;
    }

    return count;
}

void MarketDataPublisher::run() {
    running_.store(true, std::memory_order_release);

    while (running_.load(std::memory_order_acquire)) {
        if (poll() == 0) {
            std::this_thread::yield();
        }
    }

    // Drain remaining events before exiting
    (void)poll();
}

void MarketDataPublisher::stop() noexcept {
    running_.store(false, std::memory_order_release);
}

}  // namespace hft
