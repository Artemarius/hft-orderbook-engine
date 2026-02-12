# CompilerFlags.cmake — Hot-path vs cold-path compiler flag sets
#
# Hot-path (core, orderbook, matching, transport):
#   Maximum optimization, no exceptions/RTTI, strict warnings
#
# Cold-path (feed, analytics, utils, gateway):
#   Standard optimization, full C++ features allowed

if(MSVC)
    set(HOT_PATH_FLAGS
        /O2
        /GR-            # No RTTI
        /W4 /WX
        /wd5051         # [[likely]]/[[unlikely]] accepted in C++17 (GCC-compat)
        /Oy-            # Keep frame pointers for perf profiling
    )
    set(COLD_PATH_FLAGS
        /O2
        /W3
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(HOT_PATH_FLAGS
            /Od /Zi
            /GR-
            /W4 /WX
            /wd5051
            /Oy-
        )
        set(COLD_PATH_FLAGS
            /Od /Zi
            /W3
        )
    endif()
else()
    set(HOT_PATH_FLAGS
        -O3
        -march=native
        -fno-exceptions
        -fno-rtti
        -Wall -Wextra -Wpedantic -Werror
        -fno-omit-frame-pointer    # Keep frame pointers for perf profiling
    )
    set(COLD_PATH_FLAGS
        -O2
        -Wall -Wextra -Wpedantic
    )
    # Debug build: add sanitizers
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(HOT_PATH_FLAGS
            -O0 -g
            -fno-exceptions
            -fno-rtti
            -Wall -Wextra -Wpedantic -Werror
            -fno-omit-frame-pointer
            -fsanitize=address,undefined
        )
        set(COLD_PATH_FLAGS
            -O0 -g
            -Wall -Wextra -Wpedantic
            -fsanitize=address,undefined
        )
        set(SANITIZER_LINK_FLAGS -fsanitize=address,undefined)
    endif()
endif()

function(apply_hot_path_flags target)
    target_compile_options(${target} PRIVATE ${HOT_PATH_FLAGS})
    if(SANITIZER_LINK_FLAGS)
        target_link_options(${target} PRIVATE ${SANITIZER_LINK_FLAGS})
    endif()
endfunction()

function(apply_cold_path_flags target)
    target_compile_options(${target} PRIVATE ${COLD_PATH_FLAGS})
    if(SANITIZER_LINK_FLAGS)
        target_link_options(${target} PRIVATE ${SANITIZER_LINK_FLAGS})
    endif()
endfunction()
