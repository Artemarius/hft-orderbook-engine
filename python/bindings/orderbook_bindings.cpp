/// @file orderbook_bindings.cpp
/// @brief pybind11 bindings for OrderBook (read-only queries).

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <sstream>
#include <vector>

#include "core/price_level.h"
#include "core/types.h"
#include "orderbook/order_book.h"

#include "converters.h"

namespace py = pybind11;

namespace hft {
namespace python {

void bind_orderbook(py::module_& m) {
    py::class_<OrderBook>(m, "OrderBook")
        // Read-only query methods only — no mutation exposed to Python.

        .def("best_bid", [](const OrderBook& book) -> py::object {
            const PriceLevel* level = book.best_bid();
            if (!level) return py::none();
            py::dict d;
            d["price"] = level->price;
            d["price_float"] = price_to_float(level->price);
            d["quantity"] = level->total_quantity;
            d["order_count"] = level->order_count;
            return d;
        }, "Best bid level as dict, or None if empty")

        .def("best_ask", [](const OrderBook& book) -> py::object {
            const PriceLevel* level = book.best_ask();
            if (!level) return py::none();
            py::dict d;
            d["price"] = level->price;
            d["price_float"] = price_to_float(level->price);
            d["quantity"] = level->total_quantity;
            d["order_count"] = level->order_count;
            return d;
        }, "Best ask level as dict, or None if empty")

        .def("spread", &OrderBook::spread,
             "Bid-ask spread in fixed-point. Returns -1 if either side empty.")

        .def("spread_float", [](const OrderBook& book) {
            Price s = book.spread();
            if (s < 0) return -1.0;
            return price_to_float(s);
        }, "Bid-ask spread as float. Returns -1.0 if either side empty.")

        .def("mid_price", &OrderBook::mid_price,
             "Mid price in fixed-point. Returns 0 if either side empty.")

        .def("mid_price_float", [](const OrderBook& book) {
            Price mp = book.mid_price();
            if (mp == 0) return 0.0;
            return price_to_float(mp);
        }, "Mid price as float. Returns 0.0 if either side empty.")

        .def("get_bid_depth", [](const OrderBook& book, size_t n) {
            std::vector<DepthEntry> entries(n);
            size_t filled = book.get_bid_depth(entries.data(), n);
            py::list result;
            for (size_t i = 0; i < filled; ++i) {
                py::dict d;
                d["price"] = entries[i].price;
                d["price_float"] = price_to_float(entries[i].price);
                d["quantity"] = entries[i].quantity;
                d["order_count"] = entries[i].order_count;
                result.append(d);
            }
            return result;
        }, py::arg("levels") = 10,
           "Get bid depth as list of dicts, ordered best (highest) to worst.")

        .def("get_ask_depth", [](const OrderBook& book, size_t n) {
            std::vector<DepthEntry> entries(n);
            size_t filled = book.get_ask_depth(entries.data(), n);
            py::list result;
            for (size_t i = 0; i < filled; ++i) {
                py::dict d;
                d["price"] = entries[i].price;
                d["price_float"] = price_to_float(entries[i].price);
                d["quantity"] = entries[i].quantity;
                d["order_count"] = entries[i].order_count;
                result.append(d);
            }
            return result;
        }, py::arg("levels") = 10,
           "Get ask depth as list of dicts, ordered best (lowest) to worst.")

        .def_property_readonly("order_count", &OrderBook::order_count)
        .def_property_readonly("empty", &OrderBook::empty)
        .def_property_readonly("min_price", &OrderBook::min_price)
        .def_property_readonly("max_price", &OrderBook::max_price)
        .def_property_readonly("tick_size", &OrderBook::tick_size)

        .def("__repr__", [](const OrderBook& book) {
            std::ostringstream oss;
            oss << "OrderBook(orders=" << book.order_count()
                << ", spread=" << price_to_float(book.spread())
                << ", mid=" << price_to_float(book.mid_price()) << ")";
            return oss.str();
        });
}

}  // namespace python
}  // namespace hft
