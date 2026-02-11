# hft_orderbook — Order book, memory pool, price level storage, flat hash map
# Hot-path library

add_library(hft_orderbook STATIC order_book.cpp)

target_include_directories(hft_orderbook PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(hft_orderbook PUBLIC hft_core)

apply_hot_path_flags(hft_orderbook)
