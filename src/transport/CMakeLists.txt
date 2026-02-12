# hft_transport — SPSC ring buffer, message types
# Hot-path library — Phase 4 (header-only)

add_library(hft_transport INTERFACE)

target_include_directories(hft_transport INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(hft_transport INTERFACE hft_core)
