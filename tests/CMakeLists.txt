# tests/CMakeLists.txt — Google Test unit tests

include(GoogleTest)

# test_order — verifies core types and (later) Order struct
add_executable(test_order test_order.cpp)
target_link_libraries(test_order PRIVATE hft_core GTest::gtest_main)
gtest_discover_tests(test_order)
