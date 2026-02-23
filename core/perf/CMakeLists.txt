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
            add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:libzlink>"
                "$<TARGET_FILE_DIR:${name}>/zlink.dll"
            )
        endif()
    endfunction()

    add_current_bench_single(comp_current_pair single/current/bench_current_pair.cpp)
    add_current_bench_single(comp_current_pubsub single/current/bench_current_pubsub.cpp)
    add_current_bench_single(comp_current_dealer_dealer single/current/bench_current_dealer_dealer.cpp)
    add_current_bench_single(comp_current_dealer_router single/current/bench_current_dealer_router.cpp)
    add_current_bench_single(comp_current_router_router single/current/bench_current_router_router.cpp)
    add_current_bench_single(comp_current_router_router_poll single/current/bench_current_router_router_poll.cpp)
    add_current_bench_single(comp_current_stream single/current/bench_current_stream.cpp)
    add_current_bench_single(comp_current_gateway single/current/bench_current_gateway.cpp)
    add_current_bench_single(comp_current_spot single/current/bench_current_spot.cpp)

    add_current_bench_multi(comp_current_multi_dealer_dealer
                           multi/current/bench_current_multi_dealer_dealer.cpp)
    add_current_bench_multi(comp_current_multi_dealer_router
                           multi/current/bench_current_multi_dealer_router.cpp)
    add_current_bench_multi(comp_current_multi_router_router
                           multi/current/bench_current_multi_router_router.cpp)
    add_current_bench_multi(comp_current_multi_pubsub
                           multi/current/bench_current_multi_pubsub.cpp)
    add_current_bench_multi(comp_current_multi_gateway
                           multi/current/bench_current_multi_gateway.cpp)
    add_current_bench_multi(comp_current_multi_spot
                           multi/current/bench_current_multi_spot.cpp)
    add_current_bench_multi(comp_current_multi_stream
                           multi/current/bench_current_multi_stream.cpp)
else()
    message(WARNING "zlink headers not found at ${ZLINK_INCLUDE_DIR}")
endif()
