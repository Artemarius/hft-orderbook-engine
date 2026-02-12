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

# test_order_book — verifies order book, flat hash map, add/cancel/best bid/ask
add_executable(test_order_book test_order_book.cpp)
target_link_libraries(test_order_book PRIVATE hft_orderbook GTest::gtest_main)
gtest_discover_tests(test_order_book)

# test_matching_engine — verifies matching engine, order types, STP, iceberg
add_executable(test_matching_engine test_matching_engine.cpp)
target_link_libraries(test_matching_engine PRIVATE hft_matching GTest::gtest_main)
gtest_discover_tests(test_matching_engine)

# test_spsc_ring_buffer — verifies SPSC ring buffer, message types, threading
add_executable(test_spsc_ring_buffer test_spsc_ring_buffer.cpp)
target_link_libraries(test_spsc_ring_buffer PRIVATE hft_transport GTest::gtest_main)
gtest_discover_tests(test_spsc_ring_buffer)

# test_gateway — verifies order gateway, market data publisher, event pipeline
add_executable(test_gateway test_gateway.cpp)
target_link_libraries(test_gateway PRIVATE hft_gateway GTest::gtest_main)
gtest_discover_tests(test_gateway)

# test_l3_replay — verifies L3 feed parser, replay engine, end-to-end replay
add_executable(test_l3_replay test_l3_replay.cpp)
target_link_libraries(test_l3_replay PRIVATE hft_feed GTest::gtest_main)
gtest_discover_tests(test_l3_replay)
