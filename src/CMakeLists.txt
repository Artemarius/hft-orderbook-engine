# src/CMakeLists.txt — Collects all sub-libraries
#
# Hot-path libraries (Phase 0+):
add_subdirectory(core)

add_subdirectory(orderbook)      # Phase 1: memory pool; Phase 2: order book
add_subdirectory(matching)       # Phase 3
add_subdirectory(transport)      # Phase 4
add_subdirectory(gateway)        # Phase 5

# Cold-path libraries:
# add_subdirectory(feed)         # Phase 6
# add_subdirectory(analytics)    # Phase 7
# add_subdirectory(utils)        # Phase 1+
