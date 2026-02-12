# hft_gateway — Order gateway, market data publisher
# Cold-path library — Phase 5

add_library(hft_gateway STATIC
    order_gateway.cpp
    market_data_publisher.cpp
    instrument_registry.cpp
    instrument_router.cpp
)

target_include_directories(hft_gateway PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(hft_gateway PUBLIC hft_matching hft_transport hft_core)

apply_cold_path_flags(hft_gateway)
