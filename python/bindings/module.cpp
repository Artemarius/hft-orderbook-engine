/// @file module.cpp
/// @brief pybind11 module entry point for hft_orderbook Python bindings.

#include <pybind11/pybind11.h>

namespace py = pybind11;

namespace hft {
namespace python {

// Forward declarations — defined in separate binding files.
void bind_core(py::module_& m);
void bind_orderbook(py::module_& m);
void bind_replay(py::module_& m);
void bind_analytics(py::module_& m);

}  // namespace python
}  // namespace hft

PYBIND11_MODULE(hft_orderbook, m) {
    m.doc() = "High-frequency trading order book engine with analytics";

    hft::python::bind_core(m);
    hft::python::bind_orderbook(m);
    hft::python::bind_replay(m);
    hft::python::bind_analytics(m);
}
