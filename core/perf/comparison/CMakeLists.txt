
# Define standard libzlink location (from reference)
set(STD_LIBZLINK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/libzlink/libzlink_dist")
set(STD_LIBZLINK_INCLUDE_DIR "${STD_LIBZLINK_ROOT}/include")
set(STD_LIBZLINK_LIBRARY "${STD_LIBZLINK_ROOT}/lib/libzlink.so")

if(EXISTS "${STD_LIBZLINK_INCLUDE_DIR}/zlink.h" AND EXISTS "${STD_LIBZLINK_LIBRARY}")
    message(STATUS "Found standard libzlink for comparison: ${STD_LIBZLINK_LIBRARY}")
    
    # Function to add standard libzlink benchmark
    function(add_std_zlink_bench name source)
        add_executable(${name} ${source})
        # Link against standard libzlink
        target_link_libraries(${name} PRIVATE ${STD_LIBZLINK_LIBRARY} Threads::Threads)
        # Use standard libzlink headers specifically for these
        target_include_directories(${name} PRIVATE ${STD_LIBZLINK_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/common)
        
        # RPATH for running without setting LD_LIBRARY_PATH manually
        set_target_properties(${name} PROPERTIES 
            BUILD_RPATH "${STD_LIBZLINK_ROOT}/lib"
            INSTALL_RPATH "${STD_LIBZLINK_ROOT}/lib"
        )
    endfunction()

    # Function to add zlink (current project) benchmark
    function(add_zlink_bench name source)
        add_executable(${name} ${source})
        # Link against current project's libzlink (which is zlink)
        target_link_libraries(${name} PRIVATE libzlink Threads::Threads)
        target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/common)
    endfunction()

    # --- standard libzlink Benchmarks ---
    add_std_zlink_bench(comp_std_zlink_pair libzlink/bench_zlink_pair.cpp)
    add_std_zlink_bench(comp_std_zlink_router libzlink/bench_zlink_router.cpp)
    add_std_zlink_bench(comp_std_zlink_pubsub libzlink/bench_zlink_pubsub.cpp)

    # --- zlink (Current Project) Benchmarks ---
    add_zlink_bench(comp_zlink_pair zlink/bench_zlink_pair.cpp)
    add_zlink_bench(comp_zlink_router zlink/bench_zlink_router.cpp)
    add_zlink_bench(comp_zlink_pubsub zlink/bench_zlink_pubsub.cpp)

else()
    message(WARNING "Standard libzlink distribution not found at ${STD_LIBZLINK_LIBRARY}. Comparative benchmarks will be skipped.")
endif()
