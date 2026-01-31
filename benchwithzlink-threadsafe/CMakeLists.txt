# benchwithzlink-threadsafe - compare thread-safe vs non-thread-safe sockets
#
# Usage:
#   cmake -B build -DBUILD_BENCHMARKS=ON && cmake --build build
#   ./benchwithzlink-threadsafe/run_benchmarks.sh ALL

set(ZLINK_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include")

if(EXISTS "${ZLINK_INCLUDE_DIR}/zlink.h")
    set(BENCH_CXX_FLAGS "-O3")

    function(add_plain_bench name source)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE libzlink Threads::Threads)
        target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/common)
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

    function(add_threadsafe_bench name source)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE libzlink Threads::Threads)
        target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/common)
        target_compile_options(${name} PRIVATE ${BENCH_CXX_FLAGS})
        target_compile_features(${name} PRIVATE cxx_std_11)
        target_compile_definitions(${name} PRIVATE BENCH_THREADSAFE=1)

        if(WIN32)
            add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:libzlink>"
                "$<TARGET_FILE_DIR:${name}>/zlink.dll"
            )
        endif()
    endfunction()

    add_plain_bench(comp_plain_pair bench/bench_pair.cpp)
    add_plain_bench(comp_plain_pubsub bench/bench_pubsub.cpp)
    add_plain_bench(comp_plain_dealer_dealer bench/bench_dealer_dealer.cpp)
    add_plain_bench(comp_plain_dealer_router bench/bench_dealer_router.cpp)
    add_plain_bench(comp_plain_router_router bench/bench_router_router.cpp)
    add_plain_bench(comp_plain_router_router_poll bench/bench_router_router_poll.cpp)
    add_plain_bench(comp_plain_stream bench/bench_stream.cpp)

    add_threadsafe_bench(comp_threadsafe_pair bench/bench_pair.cpp)
    add_threadsafe_bench(comp_threadsafe_pubsub bench/bench_pubsub.cpp)
    add_threadsafe_bench(comp_threadsafe_dealer_dealer bench/bench_dealer_dealer.cpp)
    add_threadsafe_bench(comp_threadsafe_dealer_router bench/bench_dealer_router.cpp)
    add_threadsafe_bench(comp_threadsafe_router_router bench/bench_router_router.cpp)
    add_threadsafe_bench(comp_threadsafe_router_router_poll bench/bench_router_router_poll.cpp)
    add_threadsafe_bench(comp_threadsafe_stream bench/bench_stream.cpp)

else()
    message(WARNING "zlink headers not found at ${ZLINK_INCLUDE_DIR}")
endif()
