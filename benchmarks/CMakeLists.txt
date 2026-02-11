# benchmarks/CMakeLists.txt — Google Benchmark latency/throughput tests

add_executable(bench_orderbook bench_orderbook.cpp)
target_link_libraries(bench_orderbook PRIVATE hft_orderbook benchmark::benchmark_main)
