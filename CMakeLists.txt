cmake_minimum_required(VERSION 3.22)
project(hft-orderbook-engine
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "Low-latency order book and matching engine"
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Custom CMake modules
include(cmake/CompilerFlags.cmake)
include(cmake/Dependencies.cmake)

# Source libraries
add_subdirectory(src)

# Executables
add_executable(replay src/main/replay_main.cpp)
target_link_libraries(replay PRIVATE hft_feed)
apply_cold_path_flags(replay)

# Tests
enable_testing()
add_subdirectory(tests)

# Benchmarks
add_subdirectory(benchmarks)
