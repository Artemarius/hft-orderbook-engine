/// @file core_bindings.cpp
/// @brief pybind11 bindings for core enums, Trade, DepthEntry, and price helpers.

#include <pybind11/pybind11.h>

#include <sstream>

#include "core/trade.h"
#include "core/types.h"
#include "feed/replay_engine.h"
#include "matching/match_result.h"
#include "orderbook/order_book.h"
#include "transport/message.h"

#include "converters.h"

namespace py = pybind11;

namespace hft {
namespace python {

void bind_core(py::module_& m) {
    // --- Enums ---

    py::enum_<Side>(m, "Side")
        .value("Buy", Side::Buy)
        .value("Sell", Side::Sell)
;

    py::enum_<OrderType>(m, "OrderType")
        .value("Limit", OrderType::Limit)
        .value("Market", OrderType::Market)
        .value("IOC", OrderType::IOC)
        .value("FOK", OrderType::FOK)
        .value("GTC", OrderType::GTC)
        .value("Iceberg", OrderType::Iceberg)
;

    py::enum_<TimeInForce>(m, "TimeInForce")
        .value("GTC", TimeInForce::GTC)
        .value("IOC", TimeInForce::IOC)
        .value("FOK", TimeInForce::FOK)
        .value("DAY", TimeInForce::DAY)
;

    py::enum_<OrderStatus>(m, "OrderStatus")
        .value("New", OrderStatus::New)
        .value("Accepted", OrderStatus::Accepted)
        .value("PartialFill", OrderStatus::PartialFill)
        .value("Filled", OrderStatus::Filled)
        .value("Cancelled", OrderStatus::Cancelled)
        .value("Rejected", OrderStatus::Rejected)
;

    py::enum_<EventType>(m, "EventType")
        .value("Trade", EventType::Trade)
        .value("OrderAccepted", EventType::OrderAccepted)
        .value("OrderCancelled", EventType::OrderCancelled)
        .value("OrderRejected", EventType::OrderRejected)
        .value("OrderFilled", EventType::OrderFilled)
        .value("OrderPartialFill", EventType::OrderPartialFill)
        .value("OrderModified", EventType::OrderModified)
;

    py::enum_<MessageType>(m, "MessageType")
        .value("Add", MessageType::Add)
        .value("Cancel", MessageType::Cancel)
        .value("Modify", MessageType::Modify)
;

    py::enum_<MatchStatus>(m, "MatchStatus")
        .value("Filled", MatchStatus::Filled)
        .value("PartialFill", MatchStatus::PartialFill)
        .value("Resting", MatchStatus::Resting)
        .value("Cancelled", MatchStatus::Cancelled)
        .value("Rejected", MatchStatus::Rejected)
        .value("SelfTradePrevented", MatchStatus::SelfTradePrevented)
        .value("Modified", MatchStatus::Modified)
;

    py::enum_<SelfTradePreventionMode>(m, "SelfTradePreventionMode")
        .value("NoSTP", SelfTradePreventionMode::None)
        .value("CancelNewest", SelfTradePreventionMode::CancelNewest)
        .value("CancelOldest", SelfTradePreventionMode::CancelOldest)
        .value("CancelBoth", SelfTradePreventionMode::CancelBoth)
;

    py::enum_<PlaybackSpeed>(m, "PlaybackSpeed")
        .value("Max", PlaybackSpeed::Max)
        .value("Realtime", PlaybackSpeed::Realtime)
        .value("FastForward", PlaybackSpeed::FastForward)
;

    // --- Trade ---

    py::class_<Trade>(m, "Trade")
        .def_readonly("trade_id", &Trade::trade_id)
        .def_readonly("buy_order_id", &Trade::buy_order_id)
        .def_readonly("sell_order_id", &Trade::sell_order_id)
        .def_readonly("price", &Trade::price)
        .def_readonly("quantity", &Trade::quantity)
        .def_readonly("timestamp", &Trade::timestamp)
        .def("price_as_float", [](const Trade& t) {
            return price_to_float(t.price);
        }, "Price as floating-point value")
        .def("__repr__", [](const Trade& t) {
            std::ostringstream oss;
            oss << "Trade(id=" << t.trade_id
                << ", price=" << price_to_float(t.price)
                << ", qty=" << t.quantity
                << ", buy=" << t.buy_order_id
                << ", sell=" << t.sell_order_id << ")";
            return oss.str();
        });

    // --- DepthEntry ---

    py::class_<DepthEntry>(m, "DepthEntry")
        .def_readonly("price", &DepthEntry::price)
        .def_readonly("quantity", &DepthEntry::quantity)
        .def_readonly("order_count", &DepthEntry::order_count)
        .def("price_as_float", [](const DepthEntry& d) {
            return price_to_float(d.price);
        }, "Price as floating-point value")
        .def("__repr__", [](const DepthEntry& d) {
            std::ostringstream oss;
            oss << "DepthEntry(price=" << price_to_float(d.price)
                << ", qty=" << d.quantity
                << ", orders=" << d.order_count << ")";
            return oss.str();
        });

    // --- Constants ---

    m.attr("PRICE_SCALE") = PRICE_SCALE;
    m.attr("DEFAULT_INSTRUMENT_ID") = DEFAULT_INSTRUMENT_ID;

    // --- Price helper functions ---

    m.def("price_to_float", &price_to_float,
          py::arg("price"),
          "Convert fixed-point Price (int64) to floating-point double");

    m.def("float_to_price", &float_to_price,
          py::arg("value"),
          "Convert floating-point double to fixed-point Price (int64)");
}

}  // namespace python
}  // namespace hft
