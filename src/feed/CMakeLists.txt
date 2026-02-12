# hft_feed — L3 data replay, CSV parser
# Cold-path library — Phase 6

add_library(hft_feed STATIC
    l3_feed_parser.cpp
    replay_engine.cpp
    multi_instrument_replay_engine.cpp
)

target_include_directories(hft_feed PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(hft_feed PUBLIC
    hft_gateway
    nlohmann_json::nlohmann_json
)

apply_cold_path_flags(hft_feed)
