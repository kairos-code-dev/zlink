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
    # TODO: poll-based benchmark needs rewrite (zlink_poll removed)
    #add_current_bench_single(perf_router_router_poll single/src/perf_router_router_poll.cpp)
    # TODO: perf_gateway needs rewrite (uses removed zlink_receiver_* API)
    #add_current_bench_single(perf_gateway single/src/perf_gateway.cpp)
    # TODO: spot benchmark needs adaptation to new spot API
    #add_current_bench_single(perf_spot single/src/perf_spot.cpp)

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
    add_current_bench_multi(comp_src_pubsub_server
                           multi/src/perf_multi_pubsub_server.cpp)
    # TODO: multi benchmarks below need rewrite (zlink_poll removed, spot API changed)
    #add_current_bench_multi(comp_src_dealer_router_server
    #                       multi/src/perf_multi_dealer_router_server.cpp)
    #add_current_bench_multi(comp_src_router_router_server
    #                       multi/src/perf_multi_router_router_server.cpp)
    #add_current_bench_multi(comp_src_gateway_server
    #                       multi/src/perf_multi_gateway_server.cpp)
    #add_current_bench_multi(comp_src_dealer_dealer_client
    #                       multi/src/perf_multi_dealer_dealer_client.cpp)
    #add_current_bench_multi(comp_src_dealer_router_client
    #                       multi/src/perf_multi_dealer_router_client.cpp)
    #add_current_bench_multi(comp_src_router_router_client
    #                       multi/src/perf_multi_router_router_client.cpp)
    #add_current_bench_multi(comp_src_pubsub_client
    #                       multi/src/perf_multi_pubsub_client.cpp)
    #add_current_bench_multi(comp_src_gateway_client
    #                       multi/src/perf_multi_gateway_client.cpp)
    #add_current_bench_multi(comp_src_spot_server
    #                       multi/src/perf_multi_spot_server.cpp)
    #add_current_bench_multi(comp_src_spot_client
    #                       multi/src/perf_multi_spot_client.cpp)
    add_current_bench_multi(comp_src_stream_callback_server
                           multi/src/perf_multi_stream_callback_server.cpp)
else()
    message(WARNING "zlink headers not found at ${ZLINK_INCLUDE_DIR}")
endif()
