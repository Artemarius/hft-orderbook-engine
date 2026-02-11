# tests/CMakeLists.txt — Google Test unit tests

include(GoogleTest)

# test_order — verifies core types, Order, Trade, PriceLevel structs
add_executable(test_order test_order.cpp)
target_link_libraries(test_order PRIVATE hft_core GTest::gtest_main)
gtest_discover_tests(test_order)

# test_memory_pool — verifies slab allocator correctness and zero-alloc guarantee
add_executable(test_memory_pool test_memory_pool.cpp)
target_link_libraries(test_memory_pool PRIVATE hft_orderbook GTest::gtest_main)
gtest_discover_tests(test_memory_pool)
