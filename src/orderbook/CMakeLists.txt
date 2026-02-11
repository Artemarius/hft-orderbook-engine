# hft_orderbook — Order book, memory pool, price level storage
# Hot-path library — header-only for now (memory_pool.h is a template)

add_library(hft_orderbook INTERFACE)

target_include_directories(hft_orderbook INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(hft_orderbook INTERFACE hft_core)
