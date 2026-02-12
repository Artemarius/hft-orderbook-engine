/// @file analytics_bindings.cpp
/// @brief pybind11 bindings for AnalyticsEngine, all 6 analytics modules,
///        and MultiInstrumentAnalytics.

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <nlohmann/json.hpp>

#include "analytics/analytics_config.h"
#include "analytics/analytics_engine.h"
#include "analytics/depth_profile.h"
#include "analytics/microprice_calculator.h"
#include "analytics/multi_instrument_analytics.h"
#include "analytics/order_flow_imbalance.h"
#include "analytics/price_impact.h"
#include "analytics/realized_volatility.h"
#include "analytics/spread_analytics.h"
#include "core/types.h"
#include "gateway/instrument_router.h"
#include "orderbook/order_book.h"
#include "transport/message.h"

#include "converters.h"

namespace py = pybind11;

namespace hft {
namespace python {

void bind_analytics(py::module_& m) {

    // --- AnalyticsConfig ---

    py::class_<AnalyticsConfig>(m, "AnalyticsConfig")
        .def(py::init<>())
        .def_readwrite("imbalance_window", &AnalyticsConfig::imbalance_window)
        .def_readwrite("vol_tick_window", &AnalyticsConfig::vol_tick_window)
        .def_readwrite("vol_time_bar_ns", &AnalyticsConfig::vol_time_bar_ns)
        .def_readwrite("impact_regression_window", &AnalyticsConfig::impact_regression_window)
        .def_readwrite("depth_max_levels", &AnalyticsConfig::depth_max_levels)
        .def_readwrite("csv_path", &AnalyticsConfig::csv_path)
        .def_readwrite("json_path", &AnalyticsConfig::json_path);

    // --- TimeSeriesRow ---

    py::class_<TimeSeriesRow>(m, "TimeSeriesRow")
        .def_readonly("sequence_num", &TimeSeriesRow::sequence_num)
        .def_readonly("timestamp", &TimeSeriesRow::timestamp)
        .def_readonly("trade_price", &TimeSeriesRow::trade_price)
        .def_readonly("trade_quantity", &TimeSeriesRow::trade_quantity)
        .def_readonly("spread", &TimeSeriesRow::spread)
        .def_readonly("spread_bps", &TimeSeriesRow::spread_bps)
        .def_readonly("microprice", &TimeSeriesRow::microprice)
        .def_readonly("imbalance", &TimeSeriesRow::imbalance)
        .def_readonly("tick_vol", &TimeSeriesRow::tick_vol)
        .def_readonly("depth_imbalance", &TimeSeriesRow::depth_imbalance)
        .def_readonly("aggressor_side", &TimeSeriesRow::aggressor_side);

    // --- SpreadAnalytics ---

    py::class_<SpreadAnalytics>(m, "SpreadAnalytics")
        .def("current_spread", &SpreadAnalytics::current_spread)
        .def("current_spread_bps", &SpreadAnalytics::current_spread_bps)
        .def("avg_spread_bps", &SpreadAnalytics::avg_spread_bps)
        .def("avg_effective_spread_bps", &SpreadAnalytics::avg_effective_spread_bps)
        .def("to_dict", [](const SpreadAnalytics& s) {
            return json_to_py(s.to_json());
        }, "Analytics results as a Python dict");

    // --- MicropriceCalculator ---

    py::class_<MicropriceCalculator>(m, "MicropriceCalculator")
        .def("current_microprice", &MicropriceCalculator::current_microprice)
        .def("is_valid", &MicropriceCalculator::is_valid)
        .def("to_dict", [](const MicropriceCalculator& mp) {
            return json_to_py(mp.to_json());
        }, "Analytics results as a Python dict");

    // --- OrderFlowImbalance ---

    py::class_<OrderFlowImbalance>(m, "OrderFlowImbalance")
        .def("current_imbalance", &OrderFlowImbalance::current_imbalance)
        .def("sample_count", &OrderFlowImbalance::sample_count)
        .def("to_dict", [](const OrderFlowImbalance& ofi) {
            return json_to_py(ofi.to_json());
        }, "Analytics results as a Python dict");

    // --- RealizedVolatility ---

    py::class_<RealizedVolatility>(m, "RealizedVolatility")
        .def("tick_volatility", &RealizedVolatility::tick_volatility)
        .def("time_bar_volatility", &RealizedVolatility::time_bar_volatility)
        .def("to_dict", [](const RealizedVolatility& rv) {
            return json_to_py(rv.to_json());
        }, "Analytics results as a Python dict");

    // --- PriceImpact ---

    py::class_<PriceImpact>(m, "PriceImpact")
        .def("kyle_lambda", &PriceImpact::kyle_lambda)
        .def("avg_temporary_impact_bps", &PriceImpact::avg_temporary_impact_bps)
        .def("avg_permanent_impact_bps", &PriceImpact::avg_permanent_impact_bps)
        .def("to_dict", [](const PriceImpact& pi) {
            return json_to_py(pi.to_json());
        }, "Analytics results as a Python dict");

    // --- DepthProfile ---

    py::class_<DepthProfile>(m, "DepthProfile")
        .def("bid_depth", &DepthProfile::bid_depth)
        .def("ask_depth", &DepthProfile::ask_depth)
        .def("depth_imbalance", &DepthProfile::depth_imbalance)
        .def("to_dict", [](const DepthProfile& dp) {
            return json_to_py(dp.to_json());
        }, "Analytics results as a Python dict");

    // --- AnalyticsEngine ---

    py::class_<AnalyticsEngine>(m, "AnalyticsEngine")
        .def(py::init<const OrderBook&, const AnalyticsConfig&>(),
             py::arg("book"),
             py::arg("config") = AnalyticsConfig{},
             py::keep_alive<1, 2>())  // AnalyticsEngine keeps OrderBook alive
        .def("on_event", &AnalyticsEngine::on_event, py::arg("event"))
        .def("write_json", &AnalyticsEngine::write_json, py::arg("path"))
        .def("write_csv", &AnalyticsEngine::write_csv, py::arg("path"))
        .def("print_summary", &AnalyticsEngine::print_summary)
        .def("to_dict", [](const AnalyticsEngine& engine) {
            return json_to_py(engine.to_json());
        }, "Aggregate analytics results as a Python dict")
        .def_property_readonly("spread",
            &AnalyticsEngine::spread,
            py::return_value_policy::reference_internal)
        .def_property_readonly("microprice",
            &AnalyticsEngine::microprice,
            py::return_value_policy::reference_internal)
        .def_property_readonly("order_flow",
            &AnalyticsEngine::order_flow,
            py::return_value_policy::reference_internal)
        .def_property_readonly("volatility",
            &AnalyticsEngine::volatility,
            py::return_value_policy::reference_internal)
        .def_property_readonly("price_impact",
            &AnalyticsEngine::price_impact,
            py::return_value_policy::reference_internal)
        .def_property_readonly("depth",
            &AnalyticsEngine::depth,
            py::return_value_policy::reference_internal)
        .def_property_readonly("trade_count", &AnalyticsEngine::trade_count);

    // --- MultiInstrumentAnalytics ---

    py::class_<MultiInstrumentAnalytics>(m, "MultiInstrumentAnalytics")
        .def(py::init<const InstrumentRouter&, const AnalyticsConfig&>(),
             py::arg("router"),
             py::arg("config") = AnalyticsConfig{},
             py::keep_alive<1, 2>())  // Keeps InstrumentRouter alive
        .def("on_event", &MultiInstrumentAnalytics::on_event, py::arg("event"))
        .def("analytics",
            [](const MultiInstrumentAnalytics& mia, InstrumentId id) -> py::object {
                const AnalyticsEngine* eng = mia.analytics(id);
                if (!eng) return py::none();
                return py::cast(eng, py::return_value_policy::reference);
            },
            py::arg("instrument_id"),
            "Access per-instrument analytics. Returns None if unknown ID.")
        .def("write_json", &MultiInstrumentAnalytics::write_json, py::arg("path"))
        .def("print_summary", &MultiInstrumentAnalytics::print_summary);
}

}  // namespace python
}  // namespace hft
