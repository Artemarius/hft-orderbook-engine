# hft_core — Core types, enums, and POD structs
# Hot-path: header-only for now, will become static lib when .cpp files are added

add_library(hft_core INTERFACE)

target_include_directories(hft_core INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)
