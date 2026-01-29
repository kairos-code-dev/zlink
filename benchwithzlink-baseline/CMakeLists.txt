# Define standard libzlink location
set(STD_LIBZLINK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/libzlink/libzlink_dist")
set(STD_LIBZLINK_INCLUDE_DIR "${STD_LIBZLINK_ROOT}/include")

if(WIN32)
    # Automatically find the .lib and .dll regardless of the versioned filename
    file(GLOB STD_LIBZLINK_LIBRARY "${STD_LIBZLINK_ROOT}/lib/*.lib")
    file(GLOB STD_LIBZLINK_DLL "${STD_LIBZLINK_ROOT}/bin/*.dll")
else()
    file(GLOB STD_LIBZLINK_LIBRARY "${STD_LIBZLINK_ROOT}/lib/libzlink.so*")
endif()

if(EXISTS "${STD_LIBZLINK_INCLUDE_DIR}/zlink.h" AND STD_LIBZLINK_LIBRARY)
    message(STATUS "Found standard libzlink for comparison: ${STD_LIBZLINK_LIBRARY}")
    
    set(BENCH_CXX_FLAGS "-O3")

    function(add_std_zlink_bench name source)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE ${STD_LIBZLINK_LIBRARY} Threads::Threads)
        target_include_directories(${name} PRIVATE ${STD_LIBZLINK_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/common)
        target_compile_options(${name} PRIVATE ${BENCH_CXX_FLAGS})
        target_compile_features(${name} PRIVATE cxx_std_11)
        
        if(WIN32)
            # Copy the standard libzlink.dll to the executable directory (using the auto-detected path)
            add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${STD_LIBZLINK_DLL}"
                "$<TARGET_FILE_DIR:${name}>/libzlink.dll"
            )
        else()
            set_target_properties(${name} PROPERTIES 
                BUILD_RPATH "${STD_LIBZLINK_ROOT}/lib"
                INSTALL_RPATH "${STD_LIBZLINK_ROOT}/lib"
            )
        endif()
    endfunction()

    function(add_zlink_bench name source)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE libzlink Threads::Threads)
        target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/common)
        target_compile_options(${name} PRIVATE ${BENCH_CXX_FLAGS})
        target_compile_features(${name} PRIVATE cxx_std_11)
        
        if(WIN32)
            # Copy the custom zlink.dll to the executable directory
            add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:libzlink>"
                "$<TARGET_FILE_DIR:${name}>/zlink.dll"
            )
        endif()
    endfunction()

    # --- standard libzlink ---
    add_std_zlink_bench(comp_std_zlink_pair libzlink/bench_zlink_pair.cpp)
    add_std_zlink_bench(comp_std_zlink_pubsub libzlink/bench_zlink_pubsub.cpp)
    add_std_zlink_bench(comp_std_zlink_dealer_dealer libzlink/bench_zlink_dealer_dealer.cpp)
    add_std_zlink_bench(comp_std_zlink_dealer_router libzlink/bench_zlink_dealer_router.cpp)
    add_std_zlink_bench(comp_std_zlink_router_router libzlink/bench_zlink_router_router.cpp)
    add_std_zlink_bench(comp_std_zlink_router_router_poll libzlink/bench_zlink_router_router_poll.cpp)

    # --- zlink ---
    add_zlink_bench(comp_zlink_pair zlink/bench_zlink_pair.cpp)
    add_zlink_bench(comp_zlink_pubsub zlink/bench_zlink_pubsub.cpp)
    add_zlink_bench(comp_zlink_dealer_dealer zlink/bench_zlink_dealer_dealer.cpp)
    add_zlink_bench(comp_zlink_dealer_router zlink/bench_zlink_dealer_router.cpp)
    add_zlink_bench(comp_zlink_router_router zlink/bench_zlink_router_router.cpp)
    add_zlink_bench(comp_zlink_router_router_poll zlink/bench_zlink_router_router_poll.cpp)

else()
    message(WARNING "Standard libzlink not found. Comparative benchmarks will be skipped.")
endif()
