# core/perf - current zlink benchmark targets
#
# Build with:
#   cmake -B build -DBUILD_BENCHMARKS=ON && cmake --build build
# Run with:
#   ./core/perf/run_benchmarks.sh ALL

# Resolve current zlink include path (supports root-level and core-level builds)
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

    function(add_current_bench_single name source)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE libzlink Threads::Threads)
        if(NOT WIN32)
            target_link_libraries(${name} PRIVATE dl)
        endif()
        target_include_directories(${name} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/single/common)
        target_compile_options(${name} PRIVATE ${BENCH_CXX_FLAGS})
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
        target_compile_options(${name} PRIVATE ${BENCH_CXX_FLAGS})
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

    add_current_bench_single(perf_pair single/current/perf_pair.cpp)
    add_current_bench_single(perf_pubsub single/current/perf_pubsub.cpp)
    add_current_bench_single(perf_dealer_dealer single/current/perf_dealer_dealer.cpp)
    add_current_bench_single(perf_dealer_router single/current/perf_dealer_router.cpp)
    add_current_bench_single(perf_router_router single/current/perf_router_router.cpp)
    add_current_bench_single(perf_router_router_poll single/current/perf_router_router_poll.cpp)
    add_current_bench_single(perf_stream single/current/perf_stream.cpp)
    add_current_bench_single(perf_stream_callback single/current/perf_stream_callback.cpp)
    add_current_bench_single(perf_stream_len32be single/current/perf_stream_len32be.cpp)
    add_current_bench_single(perf_gateway single/current/perf_gateway.cpp)
    add_current_bench_single(perf_spot single/current/perf_spot.cpp)

    add_current_bench_multi(comp_current_multi_dealer_dealer_server
                           ../bench/with_zmq/multi/zlink/bench_zlink_multi_dealer_dealer_server.cpp)
    add_current_bench_multi(comp_current_multi_dealer_dealer_client
                           ../bench/with_zmq/multi/zlink/bench_zlink_multi_dealer_dealer_client.cpp)
    add_current_bench_multi(comp_current_multi_dealer_router_server
                           ../bench/with_zmq/multi/zlink/bench_zlink_multi_dealer_router_server.cpp)
    add_current_bench_multi(comp_current_multi_dealer_router_client
                           ../bench/with_zmq/multi/zlink/bench_zlink_multi_dealer_router_client.cpp)
    add_current_bench_multi(comp_current_multi_router_router_server
                           ../bench/with_zmq/multi/zlink/bench_zlink_multi_router_router_server.cpp)
    add_current_bench_multi(comp_current_multi_router_router_client
                           ../bench/with_zmq/multi/zlink/bench_zlink_multi_router_router_client.cpp)
    add_current_bench_multi(comp_current_multi_pubsub_server
                           ../bench/with_zmq/multi/zlink/bench_zlink_multi_pubsub_server.cpp)
    add_current_bench_multi(comp_current_multi_pubsub_client
                           ../bench/with_zmq/multi/zlink/bench_zlink_multi_pubsub_client.cpp)
    add_current_bench_multi(comp_current_multi_gateway_server
                           ../bench/with_zmq/multi/zlink/bench_zlink_multi_gateway_server.cpp)
    add_current_bench_multi(comp_current_multi_gateway_client
                           ../bench/with_zmq/multi/zlink/bench_zlink_multi_gateway_client.cpp)
    add_current_bench_multi(comp_current_multi_spot_server
                           ../bench/with_zmq/multi/zlink/bench_zlink_multi_spot_server.cpp)
    add_current_bench_multi(comp_current_multi_spot_client
                           ../bench/with_zmq/multi/zlink/bench_zlink_multi_spot_client.cpp)
    add_current_bench_multi(comp_current_multi_stream_server
                           ../bench/with_zmq/multi/zlink/bench_zlink_multi_stream_server.cpp)
    add_current_bench_multi(comp_current_multi_stream_client
                           ../bench/with_zmq/multi/zlink/bench_zlink_multi_stream_client.cpp)
    add_current_bench_multi(comp_current_multi_stream_callback_server
                           ../bench/with_zmq/multi/zlink/bench_zlink_multi_stream_callback_server.cpp)
    add_current_bench_multi(comp_current_multi_stream_callback_client
                           ../bench/with_zmq/multi/zlink/bench_zlink_multi_stream_callback_client.cpp)
    add_current_bench_multi(comp_current_multi_stream_len32be_server
                           ../bench/with_zmq/multi/zlink/bench_zlink_multi_stream_len32be_server.cpp)
    add_current_bench_multi(comp_current_multi_stream_len32be_client
                           ../bench/with_zmq/multi/zlink/bench_zlink_multi_stream_len32be_client.cpp)

    # STREAM variants keep a dedicated high-throughput implementation.
    add_current_bench_multi(comp_current_multi_stream_mono
                           ../bench/with_zmq/multi/zlink/bench_zlink_multi_stream.cpp)
    target_compile_definitions(
        comp_current_multi_stream_mono PRIVATE
        BENCH_MULTI_STREAM_PATTERN_NAME=\"MULTI_STREAM\"
        BENCH_MULTI_STREAM_DISPATCH_MODE_VALUE=1)

    add_current_bench_multi(comp_current_multi_stream_callback_mono
                           ../bench/with_zmq/multi/zlink/bench_zlink_multi_stream.cpp)
    target_compile_definitions(
        comp_current_multi_stream_callback_mono PRIVATE
        BENCH_MULTI_STREAM_PATTERN_NAME=\"MULTI_STREAM_CALLBACK\"
        BENCH_MULTI_STREAM_DISPATCH_MODE_VALUE=1)

    add_current_bench_multi(comp_current_multi_stream_len32be_mono
                           ../bench/with_zmq/multi/zlink/bench_zlink_multi_stream.cpp)
    target_compile_definitions(
        comp_current_multi_stream_len32be_mono PRIVATE
        BENCH_MULTI_STREAM_PATTERN_NAME=\"MULTI_STREAM_LEN32BE\"
        BENCH_MULTI_STREAM_DISPATCH_MODE_VALUE=2)
else()
    message(WARNING "zlink headers not found at ${ZLINK_INCLUDE_DIR}")
endif()
