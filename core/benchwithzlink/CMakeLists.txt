# benchwithzlink - zlink version comparison benchmarks
#
# This benchmark compares:
# - baseline: Previous zlink version (manually copied to baseline/lib/)
# - current: Current zlink build
#
# Usage:
# 1. Copy previous zlink library to benchwithzlink/baseline/lib/
#    - Linux: libzlink.so
#    - macOS: libzlink.dylib
#    - Windows: libzlink.dll + libzlink.lib
# 2. Build with: cmake -B build -DBUILD_BENCHMARKS=ON && cmake --build build
# 3. Run: ./benchwithzlink/run_benchmarks.sh ALL

# Define baseline zlink location
set(BASELINE_ZLINK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/baseline/lib")

# Find baseline library
if(WIN32)
    file(GLOB BASELINE_ZLINK_LIBRARY "${BASELINE_ZLINK_ROOT}/*.lib")
    file(GLOB BASELINE_ZLINK_DLL "${BASELINE_ZLINK_ROOT}/*.dll")
else()
    # Try to find .so first, then .dylib
    file(GLOB BASELINE_ZLINK_LIBRARY "${BASELINE_ZLINK_ROOT}/libzlink.so*")
    if(NOT BASELINE_ZLINK_LIBRARY)
        file(GLOB BASELINE_ZLINK_LIBRARY "${BASELINE_ZLINK_ROOT}/libzlink.dylib*")
    endif()
endif()

# Use current zlink's zlink.h header (API should be compatible)
set(ZLINK_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/core/include")

if(EXISTS "${ZLINK_INCLUDE_DIR}/zlink.h")
    set(BENCH_CXX_FLAGS "-O3")

    # Function to add baseline benchmarks (linked against baseline zlink)
    function(add_baseline_bench name source)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE ${BASELINE_ZLINK_LIBRARY} Threads::Threads)
        if(NOT WIN32)
            target_link_libraries(${name} PRIVATE dl)
        endif()
        target_include_directories(${name} PRIVATE ${ZLINK_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/common)
        target_compile_options(${name} PRIVATE ${BENCH_CXX_FLAGS})
        target_compile_features(${name} PRIVATE cxx_std_11)

        if(WIN32)
            # Copy the baseline libzlink.dll to the executable directory
            add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${BASELINE_ZLINK_DLL}"
                "$<TARGET_FILE_DIR:${name}>/libzlink_baseline.dll"
            )
        else()
            set_target_properties(${name} PROPERTIES
                BUILD_RPATH "${BASELINE_ZLINK_ROOT}"
                INSTALL_RPATH "${BASELINE_ZLINK_ROOT}"
            )
        endif()
    endfunction()

    # Function to add current benchmarks (linked against current zlink build)
    function(add_current_bench name source)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE libzlink Threads::Threads)
        if(NOT WIN32)
            target_link_libraries(${name} PRIVATE dl)
        endif()
        target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/common)
        target_compile_options(${name} PRIVATE ${BENCH_CXX_FLAGS})
        target_compile_features(${name} PRIVATE cxx_std_11)

        if(WIN32)
            # Copy the current zlink.dll to the executable directory
            add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:libzlink>"
                "$<TARGET_FILE_DIR:${name}>/zlink.dll"
            )
        endif()
    endfunction()

    # --- current zlink benchmarks (always build when headers exist) ---
    add_current_bench(comp_current_pair current/bench_current_pair.cpp)
    add_current_bench(comp_current_pubsub current/bench_current_pubsub.cpp)
    add_current_bench(comp_current_dealer_dealer current/bench_current_dealer_dealer.cpp)
    add_current_bench(comp_current_dealer_router current/bench_current_dealer_router.cpp)
    add_current_bench(comp_current_router_router current/bench_current_router_router.cpp)
    add_current_bench(comp_current_router_router_poll current/bench_current_router_router_poll.cpp)
    add_current_bench(comp_current_stream current/bench_current_stream.cpp)
    add_current_bench(comp_current_gateway current/bench_current_gateway.cpp)
    add_current_bench(comp_current_spot current/bench_current_spot.cpp)

    # --- baseline zlink benchmarks (optional) ---
    if(BASELINE_ZLINK_LIBRARY)
        message(STATUS "Found baseline zlink for comparison: ${BASELINE_ZLINK_LIBRARY}")
        add_baseline_bench(comp_baseline_pair baseline/bench_baseline_pair.cpp)
        add_baseline_bench(comp_baseline_pubsub baseline/bench_baseline_pubsub.cpp)
        add_baseline_bench(comp_baseline_dealer_dealer baseline/bench_baseline_dealer_dealer.cpp)
        add_baseline_bench(comp_baseline_dealer_router baseline/bench_baseline_dealer_router.cpp)
        add_baseline_bench(comp_baseline_router_router baseline/bench_baseline_router_router.cpp)
        add_baseline_bench(comp_baseline_router_router_poll baseline/bench_baseline_router_router_poll.cpp)
        add_baseline_bench(comp_baseline_stream baseline/bench_baseline_stream.cpp)
        add_baseline_bench(comp_baseline_gateway baseline/bench_baseline_gateway.cpp)
        add_baseline_bench(comp_baseline_spot baseline/bench_baseline_spot.cpp)
    else()
        message(STATUS "Baseline zlink not found in ${BASELINE_ZLINK_ROOT}. Copy previous version library there to enable comparison benchmarks.")
    endif()
else()
    message(WARNING "zlink headers not found at ${ZLINK_INCLUDE_DIR}")
endif()
