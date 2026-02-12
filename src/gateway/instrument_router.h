#pragma once

/// @file instrument_router.h
/// @brief Multi-instrument dispatch layer — routes orders to per-instrument
///        pipelines via O(1) array lookup.
///
/// Each instrument gets its own OrderBook, MemoryPool, MatchingEngine, and
/// OrderGateway. All share a single EventBuffer so downstream consumers
/// see a unified event stream tagged with instrument_id.

#include <memory>
#include <vector>

#include "core/order.h"
#include "core/types.h"
#include "gateway/instrument_registry.h"
#include "gateway/order_gateway.h"
#include "matching/matching_engine.h"
#include "orderbook/memory_pool.h"
#include "orderbook/order_book.h"
#include "transport/event_buffer.h"
#include "transport/message.h"

namespace hft {

/// A complete per-instrument processing pipeline.
struct InstrumentPipeline {
    InstrumentId instrument_id;
    std::unique_ptr<OrderBook> book;
    std::unique_ptr<MemoryPool<Order>> pool;
    std::unique_ptr<MatchingEngine> engine;
    std::unique_ptr<OrderGateway> gateway;
};

/// Routes inbound orders to the correct per-instrument pipeline.
class InstrumentRouter {
public:
    /// @param registry     Instrument definitions (must outlive this object).
    /// @param event_buffer Shared event buffer for all instruments (nullable).
    InstrumentRouter(const InstrumentRegistry& registry,
                     EventBuffer* event_buffer);

    InstrumentRouter(const InstrumentRouter&) = delete;
    InstrumentRouter& operator=(const InstrumentRouter&) = delete;

    /// Submit an order to the correct instrument pipeline.
    [[nodiscard]] GatewayResult process_order(const OrderMessage& msg) noexcept;

    /// Cancel an order on the specified instrument.
    [[nodiscard]] bool process_cancel(InstrumentId id, OrderId order_id) noexcept;

    /// Modify an order on the correct instrument pipeline.
    [[nodiscard]] GatewayResult process_modify(const OrderMessage& msg) noexcept;

    /// Access an instrument's order book. Returns nullptr if unknown id.
    [[nodiscard]] const OrderBook* order_book(InstrumentId id) const noexcept;

    /// Access a full pipeline. Returns nullptr if unknown id.
    [[nodiscard]] const InstrumentPipeline* pipeline(InstrumentId id) const noexcept;

    /// Number of registered instruments.
    [[nodiscard]] size_t instrument_count() const noexcept { return pipelines_.size(); }

private:
    /// O(1) lookup: instrument_id -> index into pipelines_.
    /// UINT32_MAX sentinel means "not mapped".
    [[nodiscard]] InstrumentPipeline* lookup(InstrumentId id) noexcept;
    [[nodiscard]] const InstrumentPipeline* lookup(InstrumentId id) const noexcept;

    std::vector<InstrumentPipeline> pipelines_;
    std::vector<size_t> id_to_index_;  // flat array, size = max_id + 1
    static constexpr size_t INVALID_INDEX = SIZE_MAX;
};

}  // namespace hft
