# hft_analytics — Market microstructure analytics
# Cold-path library — Phase 7

add_library(hft_analytics STATIC
    spread_analytics.cpp
    microprice_calculator.cpp
    order_flow_imbalance.cpp
    realized_volatility.cpp
    price_impact.cpp
    depth_profile.cpp
    analytics_engine.cpp
    multi_instrument_analytics.cpp
)
target_include_directories(hft_analytics PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)
target_link_libraries(hft_analytics PUBLIC
    hft_core hft_orderbook hft_transport hft_gateway nlohmann_json::nlohmann_json
)
apply_cold_path_flags(hft_analytics)
