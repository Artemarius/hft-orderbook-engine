#include "gateway/instrument_router.h"

#include <algorithm>

namespace hft {

InstrumentRouter::InstrumentRouter(const InstrumentRegistry& registry,
                                   EventBuffer* event_buffer) {
    const auto& instruments = registry.instruments();
    if (instruments.empty()) return;

    // Find max instrument_id to size the lookup table
    InstrumentId max_id = 0;
    for (const auto& cfg : instruments) {
        max_id = std::max(max_id, cfg.instrument_id);
    }

    id_to_index_.assign(static_cast<size_t>(max_id) + 1, INVALID_INDEX);
    pipelines_.reserve(instruments.size());

    for (const auto& cfg : instruments) {
        InstrumentPipeline pipeline;
        pipeline.instrument_id = cfg.instrument_id;
        pipeline.book = std::make_unique<OrderBook>(
            cfg.min_price, cfg.max_price, cfg.tick_size, cfg.max_orders);
        pipeline.pool = std::make_unique<MemoryPool<Order>>(cfg.max_orders);
        pipeline.engine = std::make_unique<MatchingEngine>(
            *pipeline.book, *pipeline.pool, SelfTradePreventionMode::None);
        pipeline.gateway = std::make_unique<OrderGateway>(
            *pipeline.engine, *pipeline.pool, event_buffer, cfg.instrument_id);

        size_t index = pipelines_.size();
        id_to_index_[cfg.instrument_id] = index;
        pipelines_.push_back(std::move(pipeline));
    }
}

GatewayResult InstrumentRouter::process_order(const OrderMessage& msg) noexcept {
    InstrumentPipeline* p = lookup(msg.instrument_id);
    if (!p) {
        GatewayResult result{};
        result.accepted = false;
        result.reject_reason = GatewayRejectReason::InvalidPrice;  // closest available reason
        result.match_status = MatchStatus::Rejected;
        return result;
    }
    return p->gateway->process_order(msg);
}

bool InstrumentRouter::process_cancel(InstrumentId id, OrderId order_id) noexcept {
    InstrumentPipeline* p = lookup(id);
    if (!p) return false;
    return p->gateway->process_cancel(order_id);
}

GatewayResult InstrumentRouter::process_modify(const OrderMessage& msg) noexcept {
    InstrumentPipeline* p = lookup(msg.instrument_id);
    if (!p) {
        GatewayResult result{};
        result.accepted = false;
        result.reject_reason = GatewayRejectReason::OrderNotFound;
        result.match_status = MatchStatus::Rejected;
        return result;
    }
    return p->gateway->process_modify(msg);
}

const OrderBook* InstrumentRouter::order_book(InstrumentId id) const noexcept {
    const InstrumentPipeline* p = lookup(id);
    return p ? p->book.get() : nullptr;
}

const InstrumentPipeline* InstrumentRouter::pipeline(InstrumentId id) const noexcept {
    return lookup(id);
}

InstrumentPipeline* InstrumentRouter::lookup(InstrumentId id) noexcept {
    if (id >= id_to_index_.size()) return nullptr;
    size_t index = id_to_index_[id];
    if (index == INVALID_INDEX) return nullptr;
    return &pipelines_[index];
}

const InstrumentPipeline* InstrumentRouter::lookup(InstrumentId id) const noexcept {
    if (id >= id_to_index_.size()) return nullptr;
    size_t index = id_to_index_[id];
    if (index == INVALID_INDEX) return nullptr;
    return &pipelines_[index];
}

}  // namespace hft
