# Define standard libzmq location
set(STD_LIBZMQ_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/libzmq/libzmq_dist")
set(STD_LIBZMQ_INCLUDE_DIR "${STD_LIBZMQ_ROOT}/include")
set(STD_LIBZMQ_LIBRARY "${STD_LIBZMQ_ROOT}/lib/libzmq.so")

if(EXISTS "${STD_LIBZMQ_INCLUDE_DIR}/zmq.h" AND EXISTS "${STD_LIBZMQ_LIBRARY}")
    message(STATUS "Found standard libzmq for comparison: ${STD_LIBZMQ_LIBRARY}")
    
    set(BENCH_CXX_FLAGS "-O3")

    function(add_std_zmq_bench name source)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE ${STD_LIBZMQ_LIBRARY} Threads::Threads)
        target_include_directories(${name} PRIVATE ${STD_LIBZMQ_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/common)
        target_compile_options(${name} PRIVATE ${BENCH_CXX_FLAGS})
        target_compile_features(${name} PRIVATE cxx_std_11)
        set_target_properties(${name} PROPERTIES 
            BUILD_RPATH "${STD_LIBZMQ_ROOT}/lib"
            INSTALL_RPATH "${STD_LIBZMQ_ROOT}/lib"
        )
    endfunction()

    function(add_zlink_bench name source)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE libzmq Threads::Threads)
        target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/common)
        target_compile_options(${name} PRIVATE ${BENCH_CXX_FLAGS})
        target_compile_features(${name} PRIVATE cxx_std_11)
    endfunction()

    # --- standard libzmq ---
    add_std_zmq_bench(comp_std_zmq_pair libzmq/bench_zmq_pair.cpp)
    add_std_zmq_bench(comp_std_zmq_pubsub libzmq/bench_zmq_pubsub.cpp)
    add_std_zmq_bench(comp_std_zmq_dealer_dealer libzmq/bench_zmq_dealer_dealer.cpp)
    add_std_zmq_bench(comp_std_zmq_dealer_router libzmq/bench_zmq_dealer_router.cpp)

    # --- zlink ---
    add_zlink_bench(comp_zlink_pair zlink/bench_zlink_pair.cpp)
    add_zlink_bench(comp_zlink_pubsub zlink/bench_zlink_pubsub.cpp)
    add_zlink_bench(comp_zlink_dealer_dealer zlink/bench_zlink_dealer_dealer.cpp)
    add_zlink_bench(comp_zlink_dealer_router zlink/bench_zlink_dealer_router.cpp)

else()
    message(WARNING "Standard libzmq not found. Comparative benchmarks will be skipped.")
endif()