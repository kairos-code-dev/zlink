# Define standard libzmq location
if(WIN32)
    set(STD_LIBZMQ_PLATFORM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/libzmq/libzmq_dist/windows-x64")
    if(EXISTS "${STD_LIBZMQ_PLATFORM_ROOT}")
        set(STD_LIBZMQ_ROOT "${STD_LIBZMQ_PLATFORM_ROOT}")
    else()
        set(STD_LIBZMQ_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/libzmq/libzmq_dist")
    endif()
else()
    set(STD_LIBZMQ_PLATFORM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/libzmq/libzmq_dist/linux-x64")
    if(EXISTS "${STD_LIBZMQ_PLATFORM_ROOT}")
        set(STD_LIBZMQ_ROOT "${STD_LIBZMQ_PLATFORM_ROOT}")
    else()
        set(STD_LIBZMQ_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/libzmq/libzmq_dist")
    endif()
endif()
set(STD_LIBZMQ_INCLUDE_DIR "${STD_LIBZMQ_ROOT}/include")

if(WIN32)
    # Automatically find the .lib and .dll regardless of the versioned filename
    file(GLOB STD_LIBZMQ_LIBRARY "${STD_LIBZMQ_ROOT}/lib/*.lib")
    file(GLOB STD_LIBZMQ_DLL "${STD_LIBZMQ_ROOT}/bin/*.dll")
else()
    file(GLOB STD_LIBZMQ_LIBRARY "${STD_LIBZMQ_ROOT}/lib/libzmq.so*")
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/core/include")
    set(ZLINK_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/core/include")
else()
    set(ZLINK_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include")
endif()

if(MSVC)
    set(BENCH_CXX_FLAGS "/O2")
else()
    set(BENCH_CXX_FLAGS "-O3")
endif()

function(add_std_zmq_bench name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE ${STD_LIBZMQ_LIBRARY} Threads::Threads)
    target_include_directories(${name} PRIVATE ${STD_LIBZMQ_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/common)
    target_compile_options(${name} PRIVATE ${BENCH_CXX_FLAGS})
    target_compile_features(${name} PRIVATE cxx_std_11)
    
    if(WIN32)
        # Copy all runtime DLLs to the executable directory.
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${STD_LIBZMQ_DLL}"
            "$<TARGET_FILE_DIR:${name}>"
        )
    else()
        set_target_properties(${name} PROPERTIES
            BUILD_RPATH "${STD_LIBZMQ_ROOT}/lib"
            INSTALL_RPATH "${STD_LIBZMQ_ROOT}/lib"
        )
    endif()
endfunction()

function(add_zlink_bench name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE libzlink Threads::Threads)
    target_include_directories(${name} PRIVATE ${ZLINK_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/common)
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

if(EXISTS "${ZLINK_INCLUDE_DIR}/zlink.h")
    # --- zlink (current) ---
    add_zlink_bench(comp_zlink_pair zlink/bench_zlink_pair.cpp)
    add_zlink_bench(comp_zlink_pubsub zlink/bench_zlink_pubsub.cpp)
    add_zlink_bench(comp_zlink_dealer_dealer zlink/bench_zlink_dealer_dealer.cpp)
    add_zlink_bench(comp_zlink_dealer_router zlink/bench_zlink_dealer_router.cpp)
    add_zlink_bench(comp_zlink_router_router zlink/bench_zlink_router_router.cpp)
    add_zlink_bench(comp_zlink_router_router_poll zlink/bench_zlink_router_router_poll.cpp)
    add_zlink_bench(comp_zlink_stream zlink/bench_zlink_stream.cpp)
else()
    message(WARNING "zlink headers not found at ${ZLINK_INCLUDE_DIR}")
endif()

if(EXISTS "${STD_LIBZMQ_INCLUDE_DIR}/zmq.h" AND STD_LIBZMQ_LIBRARY)
    message(STATUS "Found standard libzmq for comparison: ${STD_LIBZMQ_LIBRARY}")

    # --- standard libzmq (baseline) ---
    add_std_zmq_bench(comp_std_zmq_pair libzmq/bench_zmq_pair.cpp)
    add_std_zmq_bench(comp_std_zmq_pubsub libzmq/bench_zmq_pubsub.cpp)
    add_std_zmq_bench(comp_std_zmq_dealer_dealer libzmq/bench_zmq_dealer_dealer.cpp)
    add_std_zmq_bench(comp_std_zmq_dealer_router libzmq/bench_zmq_dealer_router.cpp)
    add_std_zmq_bench(comp_std_zmq_router_router libzmq/bench_zmq_router_router.cpp)
    add_std_zmq_bench(comp_std_zmq_router_router_poll libzmq/bench_zmq_router_router_poll.cpp)
    add_std_zmq_bench(comp_std_zmq_stream libzmq/bench_zmq_stream.cpp)
else()
    message(STATUS "Standard libzmq not found. Comparative benchmarks will be skipped.")
endif()
