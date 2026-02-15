# benchwithzlink - zlink version comparison benchmarks
#
# This benchmark compares:
# - baseline: Previous zlink version (manually copied to baseline/zlink_dist/<platform>-<arch>/)
# - current: Current zlink build
#
# Usage:
# 1. Copy previous zlink library to core/bench/benchwithzlink/baseline/zlink_dist/<platform>-<arch>/
#    - Linux: libzlink.so
#    - macOS: libzlink.dylib
#    - Windows: zlink.dll + zlink.lib
# 2. Build with: cmake -B build -DBUILD_BENCHMARKS=ON && cmake --build build
# 3. Run: ./core/bench/benchwithzlink/run_benchmarks.sh ALL

# Define baseline zlink location
if(WIN32)
    set(BASELINE_ZLINK_PLATFORM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/baseline/zlink_dist/windows-x64")
    if(EXISTS "${BASELINE_ZLINK_PLATFORM_ROOT}")
        set(BASELINE_ZLINK_ROOT "${BASELINE_ZLINK_PLATFORM_ROOT}")
    else()
        set(BASELINE_ZLINK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/baseline/lib")
    endif()
else()
    set(BASELINE_ZLINK_PLATFORM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/baseline/zlink_dist/linux-x64")
    if(EXISTS "${BASELINE_ZLINK_PLATFORM_ROOT}")
        set(BASELINE_ZLINK_ROOT "${BASELINE_ZLINK_PLATFORM_ROOT}")
    else()
        set(BASELINE_ZLINK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/baseline/lib")
    endif()
endif()

# Find baseline library
if(WIN32)
    file(GLOB BASELINE_ZLINK_LIBRARY "${BASELINE_ZLINK_ROOT}/lib/*.lib")
    file(GLOB BASELINE_ZLINK_DLL "${BASELINE_ZLINK_ROOT}/bin/*.dll")
else()
    # Try to find .so first, then .dylib
    file(GLOB BASELINE_ZLINK_LIBRARY "${BASELINE_ZLINK_ROOT}/lib/libzlink.so*")
    if(NOT BASELINE_ZLINK_LIBRARY)
        file(GLOB BASELINE_ZLINK_LIBRARY "${BASELINE_ZLINK_ROOT}/lib/libzlink.dylib*")
    endif()
endif()

# Use current zlink's zlink.h header (API should be compatible)
set(ZLINK_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include")
if(NOT EXISTS "${ZLINK_INCLUDE_DIR}/zlink.h")
    set(ZLINK_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/core/include")
endif()

if(EXISTS "${ZLINK_INCLUDE_DIR}/zlink.h")
    if(MSVC)
        set(BENCH_CXX_FLAGS "/O2")
    else()
        set(BENCH_CXX_FLAGS "-O3")
    endif()

    # Function to add baseline benchmarks (linked against baseline zlink)
    function(add_baseline_bench_single name source)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE ${BASELINE_ZLINK_LIBRARY} Threads::Threads)
        if(NOT WIN32)
            target_link_libraries(${name} PRIVATE dl)
        endif()
        target_include_directories(${name} PRIVATE ${ZLINK_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/single/common)
        target_compile_options(${name} PRIVATE ${BENCH_CXX_FLAGS})
        target_compile_features(${name} PRIVATE cxx_std_11)

        if(WIN32)
            # Copy all baseline runtime DLLs to the executable directory.
            # The baseline package may include multiple runtime DLLs.
            add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${BASELINE_ZLINK_DLL}"
                "$<TARGET_FILE_DIR:${name}>"
            )
        else()
            set_target_properties(${name} PROPERTIES
                BUILD_RPATH "${BASELINE_ZLINK_ROOT}"
                INSTALL_RPATH "${BASELINE_ZLINK_ROOT}"
            )
        endif()
    endfunction()

    # Function to add current benchmarks (linked against current zlink build)
    function(add_current_bench_single name source)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE libzlink Threads::Threads)
        if(NOT WIN32)
            target_link_libraries(${name} PRIVATE dl)
        endif()
        target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/single/common)
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
    add_current_bench_single(comp_current_pair single/current/bench_current_pair.cpp)
    add_current_bench_single(comp_current_pubsub single/current/bench_current_pubsub.cpp)
    add_current_bench_single(comp_current_dealer_dealer single/current/bench_current_dealer_dealer.cpp)
    add_current_bench_single(comp_current_dealer_router single/current/bench_current_dealer_router.cpp)
    add_current_bench_single(comp_current_router_router single/current/bench_current_router_router.cpp)
    add_current_bench_single(comp_current_router_router_poll single/current/bench_current_router_router_poll.cpp)
    add_current_bench_single(comp_current_stream single/current/bench_current_stream.cpp)
    add_current_bench_single(comp_current_gateway single/current/bench_current_gateway.cpp)
    add_current_bench_single(comp_current_spot single/current/bench_current_spot.cpp)

    function(add_current_bench_multi name source)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE libzlink Threads::Threads)
        if(NOT WIN32)
            target_link_libraries(${name} PRIVATE dl)
        endif()
        target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/multi/common)
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

    function(add_baseline_bench_multi name source)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE ${BASELINE_ZLINK_LIBRARY} Threads::Threads)
        if(NOT WIN32)
            target_link_libraries(${name} PRIVATE dl)
        endif()
        target_include_directories(${name} PRIVATE ${ZLINK_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/multi/common)
        target_compile_options(${name} PRIVATE ${BENCH_CXX_FLAGS})
        target_compile_features(${name} PRIVATE cxx_std_11)

        if(WIN32)
            # Copy all baseline runtime DLLs to the executable directory.
            # The baseline package may include multiple runtime DLLs.
            add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${BASELINE_ZLINK_DLL}"
                "$<TARGET_FILE_DIR:${name}>"
            )
        else()
            set_target_properties(${name} PROPERTIES
                BUILD_RPATH "${BASELINE_ZLINK_ROOT}"
                INSTALL_RPATH "${BASELINE_ZLINK_ROOT}"
            )
        endif()
    endfunction()

    add_current_bench_multi(comp_current_multi_dealer_dealer
                           multi/current/bench_current_multi_dealer_dealer.cpp)
    add_current_bench_multi(comp_current_multi_dealer_router
                           multi/current/bench_current_multi_dealer_router.cpp)
    add_current_bench_multi(comp_current_multi_router_router
                           multi/current/bench_current_multi_router_router.cpp)
    add_current_bench_multi(comp_current_multi_router_router_poll
                           multi/current/bench_current_multi_router_router_poll.cpp)
    add_current_bench_multi(comp_current_multi_pubsub
                           multi/current/bench_current_multi_pubsub.cpp)
    add_current_bench_multi(comp_current_multi_gateway
                           multi/current/bench_current_multi_gateway.cpp)
    add_current_bench_multi(comp_current_multi_spot
                           multi/current/bench_current_multi_spot.cpp)

    # --- baseline zlink benchmarks (optional) ---
    if(BASELINE_ZLINK_LIBRARY)
        message(STATUS "Found baseline zlink for comparison: ${BASELINE_ZLINK_LIBRARY}")
        add_baseline_bench_single(comp_baseline_pair single/baseline/bench_baseline_pair.cpp)
        add_baseline_bench_single(comp_baseline_pubsub single/baseline/bench_baseline_pubsub.cpp)
        add_baseline_bench_single(comp_baseline_dealer_dealer single/baseline/bench_baseline_dealer_dealer.cpp)
        add_baseline_bench_single(comp_baseline_dealer_router single/baseline/bench_baseline_dealer_router.cpp)
        add_baseline_bench_single(comp_baseline_router_router single/baseline/bench_baseline_router_router.cpp)
        add_baseline_bench_single(comp_baseline_router_router_poll single/baseline/bench_baseline_router_router_poll.cpp)
        add_baseline_bench_single(comp_baseline_stream single/baseline/bench_baseline_stream.cpp)
        add_baseline_bench_single(comp_baseline_gateway single/baseline/bench_baseline_gateway.cpp)
        add_baseline_bench_single(comp_baseline_spot single/baseline/bench_baseline_spot.cpp)

        add_baseline_bench_multi(comp_baseline_multi_dealer_dealer
                               multi/baseline/bench_baseline_multi_dealer_dealer.cpp)
        add_baseline_bench_multi(comp_baseline_multi_dealer_router
                               multi/baseline/bench_baseline_multi_dealer_router.cpp)
        add_baseline_bench_multi(comp_baseline_multi_router_router
                               multi/baseline/bench_baseline_multi_router_router.cpp)
        add_baseline_bench_multi(comp_baseline_multi_router_router_poll
                               multi/baseline/bench_baseline_multi_router_router_poll.cpp)
        add_baseline_bench_multi(comp_baseline_multi_pubsub
                               multi/baseline/bench_baseline_multi_pubsub.cpp)
        add_baseline_bench_multi(comp_baseline_multi_gateway
                               multi/baseline/bench_baseline_multi_gateway.cpp)
        add_baseline_bench_multi(comp_baseline_multi_spot
                               multi/baseline/bench_baseline_multi_spot.cpp)
    else()
        message(STATUS "Baseline zlink not found in ${BASELINE_ZLINK_ROOT}. Copy previous version library there to enable comparison benchmarks.")
    endif()
else()
    message(WARNING "zlink headers not found at ${ZLINK_INCLUDE_DIR}")
endif()
