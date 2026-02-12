#pragma once

/// @file converters.h
/// @brief Utility functions for C++ <-> Python conversions in pybind11 bindings.

#include <pybind11/pybind11.h>

#include <nlohmann/json.hpp>

#include "core/types.h"
#include "transport/message.h"

namespace py = pybind11;

namespace hft {
namespace python {

/// Convert fixed-point Price to floating-point double.
inline double price_to_float(Price p) {
    return static_cast<double>(p) / static_cast<double>(PRICE_SCALE);
}

/// Convert floating-point double to fixed-point Price.
inline Price float_to_price(double f) {
    return static_cast<Price>(f * static_cast<double>(PRICE_SCALE));
}

/// Recursively convert nlohmann::json to py::object.
inline py::object json_to_py(const nlohmann::json& j) {
    switch (j.type()) {
        case nlohmann::json::value_t::object: {
            py::dict d;
            for (auto it = j.begin(); it != j.end(); ++it) {
                d[py::str(it.key())] = json_to_py(it.value());
            }
            return d;
        }
        case nlohmann::json::value_t::array: {
            py::list l;
            for (const auto& elem : j) {
                l.append(json_to_py(elem));
            }
            return l;
        }
        case nlohmann::json::value_t::string:
            return py::str(j.get<std::string>());
        case nlohmann::json::value_t::boolean:
            return py::bool_(j.get<bool>());
        case nlohmann::json::value_t::number_integer:
            return py::int_(j.get<int64_t>());
        case nlohmann::json::value_t::number_unsigned:
            return py::int_(j.get<uint64_t>());
        case nlohmann::json::value_t::number_float:
            return py::float_(j.get<double>());
        case nlohmann::json::value_t::null:
        default:
            return py::none();
    }
}

/// Convert an EventMessage to a Python dict.
inline py::dict event_message_to_dict(const EventMessage& msg) {
    py::dict d;
    d["type"] = msg.type;
    d["instrument_id"] = msg.instrument_id;
    d["sequence_num"] = msg.sequence_num;

    if (msg.type == EventType::Trade) {
        const auto& t = msg.data.trade;
        py::dict trade;
        trade["trade_id"] = t.trade_id;
        trade["buy_order_id"] = t.buy_order_id;
        trade["sell_order_id"] = t.sell_order_id;
        trade["price"] = t.price;
        trade["price_float"] = price_to_float(t.price);
        trade["quantity"] = t.quantity;
        trade["timestamp"] = t.timestamp;
        d["trade"] = trade;
    } else {
        const auto& e = msg.data.order_event;
        py::dict order_event;
        order_event["order_id"] = e.order_id;
        order_event["status"] = e.status;
        order_event["filled_quantity"] = e.filled_quantity;
        order_event["remaining_quantity"] = e.remaining_quantity;
        order_event["price"] = e.price;
        order_event["price_float"] = price_to_float(e.price);
        order_event["timestamp"] = e.timestamp;
        d["order_event"] = order_event;
    }

    return d;
}

}  // namespace python
}  // namespace hft
