# core/perf - current zlink benchmark targets
#
# Build with:
#   cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON
#   cmake --build core/build
# Run with:
#   ./core/perf/run_benchmarks.sh --build-dir /home/hep7/project/kairos/zlink/core/build
#
# Notes:
#   - core/build is the only supported build directory for this repository.
#   - Single benchmarks use one target per pattern.
#   - Multi benchmarks still use per-pattern server/client binaries.

# Resolve current zlink include path (supports root-level and core-level builds)
set(ZLINK_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include")
if(NOT EXISTS "${ZLINK_INCLUDE_DIR}/zlink.h")
    set(ZLINK_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/core/include")
endif()

# Resolve external include root (supports root-level and core-level builds)
set(PERF_EXTERNAL_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/external")
if(NOT EXISTS "${PERF_EXTERNAL_INCLUDE_DIR}")
    set(PERF_EXTERNAL_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/core/external")
endif()

if(EXISTS "${ZLINK_INCLUDE_DIR}/zlink.h")
    if(MSVC)
        set(PERF_CXX_FLAGS "/O2")
    else()
        set(PERF_CXX_FLAGS "-O3")
    endif()

    function(add_current_bench_single name source)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE libzlink Threads::Threads)
        if(NOT WIN32)
            target_link_libraries(${name} PRIVATE dl)
        endif()
        target_include_directories(${name} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/single/common
            ${PERF_EXTERNAL_INCLUDE_DIR})
        target_compile_options(${name} PRIVATE ${PERF_CXX_FLAGS})
        target_compile_features(${name} PRIVATE cxx_std_11)

        if(WIN32)
            target_link_libraries(${name} PRIVATE Psapi)
            add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:libzlink>"
                "$<TARGET_FILE_DIR:${name}>/zlink.dll"
            )
        endif()
    endfunction()

    function(add_current_bench_single_recv name source)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE libzlink Threads::Threads)
        if(NOT WIN32)
            target_link_libraries(${name} PRIVATE dl)
        endif()
        target_include_directories(${name} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../bench/with_zmq/single/common
            ${CMAKE_CURRENT_SOURCE_DIR}/single/common
            ${PERF_EXTERNAL_INCLUDE_DIR})
        target_compile_options(${name} PRIVATE ${PERF_CXX_FLAGS})
        target_compile_features(${name} PRIVATE cxx_std_11)

        if(WIN32)
            target_link_libraries(${name} PRIVATE Psapi)
            add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:libzlink>"
                "$<TARGET_FILE_DIR:${name}>/zlink.dll"
            )
        endif()
    endfunction()

    function(add_current_bench_multi name source)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE libzlink Threads::Threads)
        if(NOT WIN32)
            target_link_libraries(${name} PRIVATE dl)
        endif()
        target_include_directories(${name} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/multi/common)
        target_compile_options(${name} PRIVATE ${PERF_CXX_FLAGS})
        target_compile_features(${name} PRIVATE cxx_std_11)

        if(WIN32)
            target_link_libraries(${name} PRIVATE Psapi)
            add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:libzlink>"
                "$<TARGET_FILE_DIR:${name}>/zlink.dll"
            )
        endif()
    endfunction()

    add_current_bench_single(perf_pair single/src/perf_pair.cpp)
    add_current_bench_single(perf_pubsub single/src/perf_pubsub.cpp)
    add_current_bench_single(perf_dealer_dealer single/src/perf_dealer_dealer.cpp)
    add_current_bench_single(perf_dealer_router single/src/perf_dealer_router.cpp)
    add_current_bench_single(perf_router_router single/src/perf_router_router.cpp)
    add_current_bench_single(perf_spot single/src/perf_spot.cpp)
    # Standalone STREAM client (multi-only).
    add_executable(
        perf_stream_client
        common/streamclient/perf_stream_client.cpp)
    target_link_libraries(
        perf_stream_client PRIVATE
        Threads::Threads
        OpenSSL::SSL
        OpenSSL::Crypto)
    target_compile_options(perf_stream_client PRIVATE ${PERF_CXX_FLAGS})
    target_compile_features(perf_stream_client PRIVATE cxx_std_17)

    add_current_bench_multi(comp_src_dealer_dealer_server
                           multi/src/perf_multi_dealer_dealer_server.cpp)
    add_current_bench_multi(comp_src_dealer_router_server
                           multi/src/perf_multi_dealer_router_server.cpp)
    add_current_bench_multi(comp_src_router_router_server
                           multi/src/perf_multi_router_router_server.cpp)
    add_current_bench_multi(comp_src_pubsub_server
                           multi/src/perf_multi_pubsub_server.cpp)
    add_current_bench_multi(comp_src_spot_server
                           multi/src/perf_multi_spot_server.cpp)
    add_current_bench_multi(comp_src_dealer_dealer_client
                           multi/src/perf_multi_dealer_dealer_client.cpp)
    add_current_bench_multi(comp_src_dealer_router_client
                           multi/src/perf_multi_dealer_router_client.cpp)
    add_current_bench_multi(comp_src_router_router_client
                           multi/src/perf_multi_router_router_client.cpp)
    add_current_bench_multi(comp_src_pubsub_client
                           multi/src/perf_multi_pubsub_client.cpp)
    add_current_bench_multi(comp_src_spot_client
                           multi/src/perf_multi_spot_client.cpp)
    add_current_bench_multi(comp_src_stream_server
                           multi/src/perf_multi_stream_server.cpp)
    target_compile_definitions(comp_src_stream_server PRIVATE
                               PERF_MULTI_STREAM_PATTERN_NAME="MULTI_STREAM")
else()
    message(WARNING "zlink headers not found at ${ZLINK_INCLUDE_DIR}")
endif()
