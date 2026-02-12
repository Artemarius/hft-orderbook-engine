/// @file replay_bindings.cpp
/// @brief pybind11 bindings for ReplayEngine, MultiInstrumentReplayEngine,
///        and their configs/stats.

#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "analytics/analytics_engine.h"
#include "analytics/multi_instrument_analytics.h"
#include "core/types.h"
#include "feed/multi_instrument_replay_engine.h"
#include "feed/replay_engine.h"
#include "gateway/instrument_registry.h"
#include "gateway/instrument_router.h"
#include "transport/message.h"

#include "converters.h"

namespace py = pybind11;

namespace hft {
namespace python {

void bind_replay(py::module_& m) {

    // --- InstrumentRouter (opaque, used by MultiInstrumentAnalytics) ---

    py::class_<InstrumentRouter>(m, "InstrumentRouter")
        .def("order_book",
            [](const InstrumentRouter& router, InstrumentId id) -> py::object {
                const OrderBook* book = router.order_book(id);
                if (!book) return py::none();
                return py::cast(book, py::return_value_policy::reference);
            },
            py::arg("instrument_id"),
            "Access an instrument's order book by ID. Returns None if unknown.")
        .def_property_readonly("instrument_count", &InstrumentRouter::instrument_count);

    // --- ReplayConfig ---

    py::class_<ReplayConfig>(m, "ReplayConfig")
        .def(py::init<>())
        .def_readwrite("input_path", &ReplayConfig::input_path)
        .def_readwrite("output_path", &ReplayConfig::output_path)
        .def_readwrite("speed", &ReplayConfig::speed)
        .def_readwrite("speed_multiplier", &ReplayConfig::speed_multiplier)
        .def_readwrite("min_price", &ReplayConfig::min_price)
        .def_readwrite("max_price", &ReplayConfig::max_price)
        .def_readwrite("tick_size", &ReplayConfig::tick_size)
        .def_readwrite("max_orders", &ReplayConfig::max_orders)
        .def_readwrite("enable_publisher", &ReplayConfig::enable_publisher)
        .def_readwrite("verbose", &ReplayConfig::verbose);

    // --- ReplayStats ---

    py::class_<ReplayStats>(m, "ReplayStats")
        .def_readonly("total_messages", &ReplayStats::total_messages)
        .def_readonly("add_messages", &ReplayStats::add_messages)
        .def_readonly("cancel_messages", &ReplayStats::cancel_messages)
        .def_readonly("trade_messages", &ReplayStats::trade_messages)
        .def_readonly("parse_errors", &ReplayStats::parse_errors)
        .def_readonly("orders_accepted", &ReplayStats::orders_accepted)
        .def_readonly("orders_rejected", &ReplayStats::orders_rejected)
        .def_readonly("orders_cancelled", &ReplayStats::orders_cancelled)
        .def_readonly("trades_generated", &ReplayStats::trades_generated)
        .def_readonly("cancel_failures", &ReplayStats::cancel_failures)
        .def_readonly("modify_messages", &ReplayStats::modify_messages)
        .def_readonly("orders_modified", &ReplayStats::orders_modified)
        .def_readonly("modify_failures", &ReplayStats::modify_failures)
        .def_readonly("final_best_bid", &ReplayStats::final_best_bid)
        .def_readonly("final_best_ask", &ReplayStats::final_best_ask)
        .def_readonly("final_spread", &ReplayStats::final_spread)
        .def_readonly("final_order_count", &ReplayStats::final_order_count)
        .def_readonly("elapsed_seconds", &ReplayStats::elapsed_seconds)
        .def_readonly("messages_per_second", &ReplayStats::messages_per_second)
        .def("to_dict", [](const ReplayStats& s) {
            py::dict d;
            d["total_messages"] = s.total_messages;
            d["add_messages"] = s.add_messages;
            d["cancel_messages"] = s.cancel_messages;
            d["trade_messages"] = s.trade_messages;
            d["parse_errors"] = s.parse_errors;
            d["orders_accepted"] = s.orders_accepted;
            d["orders_rejected"] = s.orders_rejected;
            d["orders_cancelled"] = s.orders_cancelled;
            d["trades_generated"] = s.trades_generated;
            d["cancel_failures"] = s.cancel_failures;
            d["modify_messages"] = s.modify_messages;
            d["orders_modified"] = s.orders_modified;
            d["modify_failures"] = s.modify_failures;
            d["final_best_bid"] = price_to_float(s.final_best_bid);
            d["final_best_ask"] = price_to_float(s.final_best_ask);
            d["final_spread"] = price_to_float(s.final_spread);
            d["final_order_count"] = s.final_order_count;
            d["elapsed_seconds"] = s.elapsed_seconds;
            d["messages_per_second"] = s.messages_per_second;
            return d;
        }, "Convert stats to a Python dict");

    // --- ReplayEngine ---

    py::class_<ReplayEngine>(m, "ReplayEngine")
        .def(py::init<const ReplayConfig&>(), py::arg("config"))
        .def("run", &ReplayEngine::run,
             "Run replay to completion. Returns ReplayStats.")
        .def_property_readonly("order_book",
            &ReplayEngine::order_book,
            py::return_value_policy::reference_internal,
            "Access the order book (valid after run() completes)")
        .def("register_event_callback",
            [](ReplayEngine& engine, py::function callback) {
                engine.register_event_callback(
                    [callback](const EventMessage& msg) {
                        py::gil_scoped_acquire acquire;
                        callback(event_message_to_dict(msg));
                    });
            },
            py::arg("callback"),
            "Register a Python callback to receive event dicts during replay")
        .def("register_analytics",
            [](ReplayEngine& engine, AnalyticsEngine& analytics) {
                engine.register_event_callback(
                    [&analytics](const EventMessage& msg) {
                        analytics.on_event(msg);
                    });
            },
            py::arg("analytics"),
            py::keep_alive<1, 2>(),
            "Wire an AnalyticsEngine to receive events directly (no Python overhead)");

    // --- InstrumentConfig ---

    py::class_<InstrumentConfig>(m, "InstrumentConfig")
        .def(py::init<>())
        .def_readwrite("instrument_id", &InstrumentConfig::instrument_id)
        .def_readwrite("symbol", &InstrumentConfig::symbol)
        .def_readwrite("min_price", &InstrumentConfig::min_price)
        .def_readwrite("max_price", &InstrumentConfig::max_price)
        .def_readwrite("tick_size", &InstrumentConfig::tick_size)
        .def_readwrite("max_orders", &InstrumentConfig::max_orders);

    // --- MultiReplayConfig ---

    py::class_<MultiReplayConfig>(m, "MultiReplayConfig")
        .def(py::init<>())
        .def_readwrite("input_path", &MultiReplayConfig::input_path)
        .def_readwrite("output_path", &MultiReplayConfig::output_path)
        .def_readwrite("instruments", &MultiReplayConfig::instruments)
        .def_readwrite("auto_discover", &MultiReplayConfig::auto_discover)
        .def_readwrite("default_min_price", &MultiReplayConfig::default_min_price)
        .def_readwrite("default_max_price", &MultiReplayConfig::default_max_price)
        .def_readwrite("default_tick_size", &MultiReplayConfig::default_tick_size)
        .def_readwrite("default_max_orders", &MultiReplayConfig::default_max_orders)
        .def_readwrite("verbose", &MultiReplayConfig::verbose);

    // --- PerInstrumentStats ---

    py::class_<PerInstrumentStats>(m, "PerInstrumentStats")
        .def_readonly("instrument_id", &PerInstrumentStats::instrument_id)
        .def_readonly("symbol", &PerInstrumentStats::symbol)
        .def_readonly("add_messages", &PerInstrumentStats::add_messages)
        .def_readonly("cancel_messages", &PerInstrumentStats::cancel_messages)
        .def_readonly("modify_messages", &PerInstrumentStats::modify_messages)
        .def_readonly("trade_messages", &PerInstrumentStats::trade_messages)
        .def_readonly("orders_accepted", &PerInstrumentStats::orders_accepted)
        .def_readonly("orders_rejected", &PerInstrumentStats::orders_rejected)
        .def_readonly("orders_cancelled", &PerInstrumentStats::orders_cancelled)
        .def_readonly("cancel_failures", &PerInstrumentStats::cancel_failures)
        .def_readonly("orders_modified", &PerInstrumentStats::orders_modified)
        .def_readonly("modify_failures", &PerInstrumentStats::modify_failures)
        .def_readonly("trades_generated", &PerInstrumentStats::trades_generated)
        .def_readonly("final_best_bid", &PerInstrumentStats::final_best_bid)
        .def_readonly("final_best_ask", &PerInstrumentStats::final_best_ask)
        .def_readonly("final_order_count", &PerInstrumentStats::final_order_count);

    // --- MultiReplayStats ---

    py::class_<MultiReplayStats>(m, "MultiReplayStats")
        .def_readonly("total_messages", &MultiReplayStats::total_messages)
        .def_readonly("parse_errors", &MultiReplayStats::parse_errors)
        .def_readonly("elapsed_seconds", &MultiReplayStats::elapsed_seconds)
        .def_readonly("messages_per_second", &MultiReplayStats::messages_per_second)
        .def_readonly("per_instrument", &MultiReplayStats::per_instrument)
        .def("to_dict", [](const MultiReplayStats& s) {
            py::dict d;
            d["total_messages"] = s.total_messages;
            d["parse_errors"] = s.parse_errors;
            d["elapsed_seconds"] = s.elapsed_seconds;
            d["messages_per_second"] = s.messages_per_second;
            py::list instruments;
            for (const auto& ps : s.per_instrument) {
                py::dict pd;
                pd["instrument_id"] = ps.instrument_id;
                pd["symbol"] = ps.symbol;
                pd["add_messages"] = ps.add_messages;
                pd["cancel_messages"] = ps.cancel_messages;
                pd["trades_generated"] = ps.trades_generated;
                pd["final_order_count"] = ps.final_order_count;
                instruments.append(pd);
            }
            d["per_instrument"] = instruments;
            return d;
        }, "Convert stats to a Python dict");

    // --- MultiInstrumentReplayEngine ---

    py::class_<MultiInstrumentReplayEngine>(m, "MultiInstrumentReplayEngine")
        .def(py::init<const MultiReplayConfig&>(), py::arg("config"))
        .def("run", &MultiInstrumentReplayEngine::run,
             "Run multi-instrument replay to completion. Returns MultiReplayStats.")
        .def("register_event_callback",
            [](MultiInstrumentReplayEngine& engine, py::function callback) {
                engine.register_event_callback(
                    [callback](const EventMessage& msg) {
                        py::gil_scoped_acquire acquire;
                        callback(event_message_to_dict(msg));
                    });
            },
            py::arg("callback"),
            "Register a Python callback to receive event dicts during replay")
        .def("register_analytics",
            [](MultiInstrumentReplayEngine& engine, MultiInstrumentAnalytics& analytics) {
                engine.register_event_callback(
                    [&analytics](const EventMessage& msg) {
                        analytics.on_event(msg);
                    });
            },
            py::arg("analytics"),
            py::keep_alive<1, 2>(),
            "Wire MultiInstrumentAnalytics to receive events directly (no Python overhead)")
        .def("router",
            &MultiInstrumentReplayEngine::router,
            py::return_value_policy::reference_internal,
            "Access the InstrumentRouter")
        .def("order_book",
            [](const MultiInstrumentReplayEngine& engine, InstrumentId id) -> py::object {
                const OrderBook* book = engine.router().order_book(id);
                if (!book) return py::none();
                return py::cast(book, py::return_value_policy::reference);
            },
            py::arg("instrument_id"),
            "Access an instrument's order book. Returns None if unknown ID.");
}

}  // namespace python
}  // namespace hft
