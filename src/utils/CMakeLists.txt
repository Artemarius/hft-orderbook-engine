# hft_utils — Clock, latency histogram, logging, config
# Header-only interface library (hot-path timing + cold-path stats)

add_library(hft_utils INTERFACE)
target_include_directories(hft_utils INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)
