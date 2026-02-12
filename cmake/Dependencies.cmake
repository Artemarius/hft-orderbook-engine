# Dependencies.cmake — FetchContent for all external dependencies
#
# All dependencies are cold-path only. Nothing external on the hot path.

include(FetchContent)

# Google Test
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
)

# Google Benchmark
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Disable benchmark testing" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "Disable benchmark install" FORCE)
FetchContent_Declare(googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.9.1
)

# spdlog — structured logging (cold path only)
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.15.0
)

# nlohmann/json — config and analytics output (cold path only)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)

# TBB — thread pool for analytics (cold path only, needed in Phase 7)
# Use system package when available: sudo apt install libtbb-dev
find_package(TBB QUIET)
if(TBB_FOUND)
    message(STATUS "Found system TBB: ${TBB_VERSION}")
else()
    message(STATUS "TBB not found — install via 'sudo apt install libtbb-dev' when needed (Phase 7)")
endif()

# pybind11 — Python bindings (cold path only, guarded by option)
if(BUILD_PYTHON_BINDINGS)
    FetchContent_Declare(pybind11
        GIT_REPOSITORY https://github.com/pybind/pybind11.git
        GIT_TAG        v2.13.6
    )
    FetchContent_MakeAvailable(googletest googlebenchmark spdlog nlohmann_json pybind11)
else()
    FetchContent_MakeAvailable(googletest googlebenchmark spdlog nlohmann_json)
endif()
