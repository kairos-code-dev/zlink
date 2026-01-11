# libzmq CMake Configuration
cmake_minimum_required(VERSION 3.5)
project(zlink VERSION 0.1.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CheckCXXCompilerFlag)
find_package(Threads REQUIRED)

# Optimization Flags
if(MSVC)
    add_compile_options(/O2 /GL /MP)
    add_link_options(/LTCG)
else()
    add_compile_options(-O3 -march=native)
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
    src/stream_listener_base.cpp src/tipc_address.cpp src/tipc_connecter.cpp 
    src/tipc_listener.cpp src/plain_client.cpp src/plain_server.cpp
    src/proxy.cpp
)

add_library(libzmq SHARED ${libzmq_sources})
set_target_properties(libzmq PROPERTIES OUTPUT_NAME zmq)
target_include_directories(libzmq PUBLIC include src)
target_link_libraries(libzmq PRIVATE Threads::Threads)

# Add Benchmarks
add_subdirectory(benchwithzmq)