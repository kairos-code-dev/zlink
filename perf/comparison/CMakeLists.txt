
# Define standard libzmq location (from reference)
set(STD_LIBZMQ_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/libzmq/libzmq_dist")
set(STD_LIBZMQ_INCLUDE_DIR "${STD_LIBZMQ_ROOT}/include")
set(STD_LIBZMQ_LIBRARY "${STD_LIBZMQ_ROOT}/lib/libzmq.so")

if(EXISTS "${STD_LIBZMQ_INCLUDE_DIR}/zmq.h" AND EXISTS "${STD_LIBZMQ_LIBRARY}")
    message(STATUS "Found standard libzmq for comparison: ${STD_LIBZMQ_LIBRARY}")
    
    # Function to add standard libzmq benchmark
    function(add_std_zmq_bench name source)
        add_executable(${name} ${source})
        # Link against standard libzmq
        target_link_libraries(${name} PRIVATE ${STD_LIBZMQ_LIBRARY} Threads::Threads)
        # Use standard libzmq headers specifically for these
        target_include_directories(${name} PRIVATE ${STD_LIBZMQ_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/common)
        
        # RPATH for running without setting LD_LIBRARY_PATH manually
        set_target_properties(${name} PROPERTIES 
            BUILD_RPATH "${STD_LIBZMQ_ROOT}/lib"
            INSTALL_RPATH "${STD_LIBZMQ_ROOT}/lib"
        )
    endfunction()

    # Function to add zlink (current project) benchmark
    function(add_zlink_bench name source)
        add_executable(${name} ${source})
        # Link against current project's libzmq (which is zlink)
        target_link_libraries(${name} PRIVATE libzmq Threads::Threads)
        target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/common)
    endfunction()

    # --- standard libzmq Benchmarks ---
    add_std_zmq_bench(comp_std_zmq_pair libzmq/bench_zmq_pair.cpp)
    add_std_zmq_bench(comp_std_zmq_router libzmq/bench_zmq_router.cpp)
    add_std_zmq_bench(comp_std_zmq_pubsub libzmq/bench_zmq_pubsub.cpp)

    # --- zlink (Current Project) Benchmarks ---
    add_zlink_bench(comp_zlink_pair zlink/bench_zlink_pair.cpp)
    add_zlink_bench(comp_zlink_router zlink/bench_zlink_router.cpp)
    add_zlink_bench(comp_zlink_pubsub zlink/bench_zlink_pubsub.cpp)

else()
    message(WARNING "Standard libzmq distribution not found at ${STD_LIBZMQ_LIBRARY}. Comparative benchmarks will be skipped.")
endif()
