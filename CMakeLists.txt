# libzmq CMake Configuration
cmake_minimum_required(VERSION 3.5)
project(zlink VERSION 0.1.4 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CheckCXXCompilerFlag)
find_package(Threads REQUIRED)

# Optimization Flags - Aggressive Optimization for C++11
if(MSVC)
    add_compile_options(/O2 /GL /MP /Oi /Ot /Oy)
    add_link_options(/LTCG)
else()
    # -O3: Max optimization
    # -march=native: Use all CPU instructions
    # -flto: Link Time Optimization (Critical for inproc performance)
    # -fno-exceptions -fno-rtti: Remove C++ runtime overhead (optional, but good for raw speed)
    add_compile_options(-O3 -march=native -flto -funroll-loops -fomit-frame-pointer)
    add_link_options(-flto -O3)
endif()

# --- Platform Configuration ---
if(MSVC)
    set(ZMQ_HAVE_WINDOWS 1)
    set(ZMQ_HAVE_NOEXCEPT 1)
    
    # Windows specific networking and polling
    set(ZMQ_IOTHREAD_POLLER_USE_EPOLL 1)
    set(ZMQ_POLL_BASED_ON_POLL 1)
    set(ZMQ_HAVE_IF_NAMETOINDEX 0)
    
    # Condition variable implementation (Corrected: Added _IMPL_)
    set(ZMQ_USE_CV_IMPL_STL11 1)
    
    # Other Windows essentials
    set(ZMQ_HAVE_SOCK_CLOEXEC 0)
    set(ZMQ_HAVE_SO_KEEPALIVE 1)
    set(ZMQ_HAVE_TCP_KEEPCNT 1)
    set(ZMQ_HAVE_TCP_KEEPIDLE 1)
    set(ZMQ_HAVE_TCP_KEEPINTVL 1)
    
    # Alignment and Atomics
    set(ZMQ_CACHELINE_SIZE 64)
    set(ZMQ_HAVE_ATOMIC_INTRINSICS 1)
    
    # Define macros for platform.hpp
    configure_file(builds/cmake/platform.hpp.in platform.hpp)
    
    # Global definitions for MSVC
    add_definitions(
        -DZMQ_HAVE_WINDOWS 
        -DZMQ_USE_CV_IMPL_STL11 
        -DZMQ_IOTHREAD_POLLER_USE_EPOLL
        -DZMQ_EXPORTS
        -D_CRT_SECURE_NO_WARNINGS 
        -D_WINSOCK_DEPRECATED_NO_WARNINGS
    )
else()
    configure_file(builds/cmake/platform.hpp.in platform.hpp)
endif()

# Core zmq sources
set(libzmq_sources
    src/address.cpp src/clock.cpp src/ctx.cpp src/dealer.cpp 
    src/decoder_allocators.cpp src/devpoll.cpp src/dist.cpp 
    src/endpoint.cpp src/epoll.cpp src/err.cpp src/fq.cpp 
    src/io_object.cpp src/io_thread.cpp src/ip.cpp 
    src/ipc_address.cpp src/ipc_connecter.cpp src/ipc_listener.cpp 
    src/kqueue.cpp src/lb.cpp src/mailbox.cpp src/mailbox_safe.cpp 
    src/mechanism.cpp src/mechanism_base.cpp src/metadata.cpp 
    src/msg.cpp src/mtrie.cpp src/null_mechanism.cpp 
    src/object.cpp src/options.cpp src/own.cpp src/pair.cpp 
    src/pgm_receiver.cpp src/pgm_sender.cpp src/pgm_socket.cpp 
    src/pipe.cpp src/poll.cpp src/poller_base.cpp src/polling_util.cpp 
    src/pollset.cpp src/pub.cpp src/radix_tree.cpp src/random.cpp 
    src/raw_decoder.cpp src/raw_encoder.cpp src/raw_engine.cpp 
    src/reaper.cpp src/router.cpp src/select.cpp src/session_base.cpp 
    src/signaler.cpp src/socket_base.cpp src/socks.cpp 
    src/socks_connecter.cpp src/stream.cpp src/stream_engine_base.cpp 
    src/sub.cpp src/tcp.cpp src/tcp_address.cpp src/tcp_connecter.cpp 
    src/tcp_listener.cpp src/thread.cpp src/timers.cpp src/trie.cpp 
    src/v1_decoder.cpp src/v1_encoder.cpp src/v2_decoder.cpp 
    src/v2_encoder.cpp src/v3_1_encoder.cpp src/xpub.cpp src/xsub.cpp 
    src/zmq.cpp src/zmq_utils.cpp src/zmtp_engine.cpp src/socket_poller.cpp 
    src/ip_resolver.cpp src/zap_client.cpp src/stream_connecter_base.cpp 
    src/stream_listener_base.cpp src/plain_client.cpp src/plain_server.cpp
    src/proxy.cpp
)

# Windows specific sources (wepoll for epoll support)
if(MSVC)
    list(APPEND libzmq_sources external/wepoll/wepoll.c)
endif()

add_library(libzmq SHARED ${libzmq_sources})
set_target_properties(libzmq PROPERTIES 
    OUTPUT_NAME zmq
    DEFINE_SYMBOL ZMQ_EXPORTS
)

# Important: Add CMAKE_CURRENT_BINARY_DIR to include paths for generated platform.hpp
target_include_directories(libzmq PUBLIC 
    include 
    src 
    external/wepoll
    ${CMAKE_CURRENT_BINARY_DIR}
)

if(MSVC)
    # Ensure standard compliance and linking
    target_compile_options(libzmq PRIVATE /EHsc)
    target_link_libraries(libzmq PRIVATE ws2_32 iphlpapi)
    
    # Post-build: Copy DLL to tests directory for convenience
    add_custom_command(TARGET libzmq POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:libzmq>
        ${CMAKE_CURRENT_BINARY_DIR}/tests/$<CONFIG>/
        COMMENT "Copying zmq.dll to tests directory..."
    )
endif()
target_link_libraries(libzmq PRIVATE Threads::Threads)

# --- Configuration for Tests ---
set(ZeroMQ_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(ZeroMQ_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(BUILD_SHARED ON)
set(BUILD_STATIC OFF) 
# Note: We are building SHARED by default. 
# tests/CMakeLists.txt expects 'testutil' to link against 'libzmq'.

enable_testing()
add_subdirectory(tests)

# Add Benchmarks
add_subdirectory(benchwithzmq)