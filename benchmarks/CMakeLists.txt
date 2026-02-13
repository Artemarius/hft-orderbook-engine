# benchmarks/CMakeLists.txt — Google Benchmark latency/throughput tests

# Apply sanitizer link flags to benchmark executables (GCC/Clang Debug builds)
function(add_hft_bench target)
    if(SANITIZER_LINK_FLAGS)
        target_compile_options(${target} PRIVATE ${SANITIZER_LINK_FLAGS})
        target_link_options(${target} PRIVATE ${SANITIZER_LINK_FLAGS})
    endif()
endfunction()

add_executable(bench_orderbook bench_orderbook.cpp)
target_link_libraries(bench_orderbook PRIVATE hft_orderbook benchmark::benchmark_main)
add_hft_bench(bench_orderbook)

add_executable(bench_matching bench_matching.cpp)
target_link_libraries(bench_matching PRIVATE hft_matching benchmark::benchmark_main)
add_hft_bench(bench_matching)

add_executable(bench_spsc bench_spsc.cpp)
target_link_libraries(bench_spsc PRIVATE hft_transport benchmark::benchmark_main)
add_hft_bench(bench_spsc)

# Custom latency histogram benchmark (rdtsc-based percentiles, standalone main)
add_executable(bench_latency bench_latency.cpp)
target_link_libraries(bench_latency PRIVATE hft_matching hft_transport hft_utils)
add_hft_bench(bench_latency)
