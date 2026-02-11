# hft_matching — Matching engine, self-trade prevention
# Hot-path library — Phase 3

add_library(hft_matching STATIC matching_engine.cpp)

target_include_directories(hft_matching PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(hft_matching PUBLIC hft_orderbook hft_core)

apply_hot_path_flags(hft_matching)
