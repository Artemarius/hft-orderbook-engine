# python/CMakeLists.txt — pybind11 Python bindings for hft-orderbook-engine
#
# Cold-path module: needs RTTI and exceptions (pybind11 requirement).
# Drops -Werror / /WX since pybind11 headers generate warnings.

pybind11_add_module(hft_python
    bindings/module.cpp
    bindings/core_bindings.cpp
    bindings/orderbook_bindings.cpp
    bindings/replay_bindings.cpp
    bindings/analytics_bindings.cpp
)

# The Python module should be importable as "hft_orderbook".
set_target_properties(hft_python PROPERTIES
    OUTPUT_NAME hft_orderbook
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Multi-config generators (Visual Studio, Ninja Multi-Config) append config
# subdirectories — flatten them so the .pyd lands in python/.
foreach(cfg_type ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${cfg_type} cfg_upper)
    set_target_properties(hft_python PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY_${cfg_upper} "${CMAKE_CURRENT_SOURCE_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_${cfg_upper} "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endforeach()

target_include_directories(hft_python PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/bindings"
)

target_link_libraries(hft_python PRIVATE
    hft_feed
    hft_analytics
    hft_gateway
    hft_matching
    hft_orderbook
    hft_core
    nlohmann_json::nlohmann_json
)

# Apply cold-path optimization flags but strip -Werror / /WX because
# pybind11 headers generate warnings we don't control.
if(MSVC)
    target_compile_options(hft_python PRIVATE
        /O2
        /W3
        /wd5051
        /wd4324
    )
else()
    target_compile_options(hft_python PRIVATE
        -O2
        -Wall -Wextra
    )
endif()
