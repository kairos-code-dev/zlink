cmake_minimum_required(VERSION 3.10)
project(zlink_cpp_perf LANGUAGES CXX)

if(NOT TARGET libzlink)
  message(FATAL_ERROR "libzlink target not found. Build core first or add_subdirectory(core)")
endif()

find_package(Threads REQUIRED)

set(PERF_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(PERF_CPP_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/bindings/cpp/include")
set(PERF_CORE_INCLUDE_DIR "${ZLINK_CORE_DIR}/include")

set(PERF_SINGLE_COMMON_SRCS
  "${PERF_ROOT_DIR}/single/common/perf_single_common.cpp"
  "${PERF_ROOT_DIR}/single/common/perf_single_runner.cpp"
)

set(PERF_MULTI_SERVER_COMMON_SRCS
  "${PERF_ROOT_DIR}/multi/common/perf_server_runner.cpp"
)

set(PERF_MULTI_CLIENT_COMMON_SRCS
  "${PERF_ROOT_DIR}/multi/common/perf_client_runner.cpp"
)

function(configure_perf_target target_name out_subdir)
  target_compile_features(${target_name} PRIVATE cxx_std_17)
  target_link_libraries(${target_name} PRIVATE libzlink zlink-cpp Threads::Threads)
  target_include_directories(${target_name} PRIVATE
    "${PERF_CPP_INCLUDE_DIR}"
    "${PERF_CORE_INCLUDE_DIR}"
    "${PERF_ROOT_DIR}/single/common"
    "${PERF_ROOT_DIR}/multi/common"
  )

  if(MSVC)
    target_compile_options(${target_name} PRIVATE /O2 /W4)
  else()
    target_compile_options(${target_name} PRIVATE -O3 -Wall -Wextra -Wpedantic)
  endif()

  set_target_properties(${target_name} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PERF_ROOT_DIR}/${out_subdir}/build"
  )
endfunction()

function(add_cpp_single_bench target_name src_path run_fn)
  add_executable(${target_name}
    ${PERF_SINGLE_COMMON_SRCS}
    "${src_path}"
  )
  target_compile_definitions(${target_name} PRIVATE RUN_PATTERN_FN=${run_fn})
  configure_perf_target(${target_name} "single")
endfunction()

function(add_cpp_multi_server_bench target_name src_path run_fn)
  add_executable(${target_name}
    ${PERF_MULTI_SERVER_COMMON_SRCS}
    "${src_path}"
  )
  target_compile_definitions(${target_name} PRIVATE RUN_MULTI_SERVER_FN=${run_fn})
  configure_perf_target(${target_name} "multi")
endfunction()

function(add_cpp_multi_client_bench target_name src_path run_fn)
  add_executable(${target_name}
    ${PERF_MULTI_CLIENT_COMMON_SRCS}
    "${src_path}"
  )
  target_compile_definitions(${target_name} PRIVATE RUN_MULTI_CLIENT_FN=${run_fn})
  configure_perf_target(${target_name} "multi")
endfunction()

# Single suite (8 binaries)
add_cpp_single_bench(perf_pair
  "${PERF_ROOT_DIR}/single/src/perf_pair.cpp"
  run_pattern_pair)
add_cpp_single_bench(perf_pubsub
  "${PERF_ROOT_DIR}/single/src/perf_pubsub.cpp"
  run_pattern_pubsub)
add_cpp_single_bench(perf_dealer_dealer
  "${PERF_ROOT_DIR}/single/src/perf_dealer_dealer.cpp"
  run_pattern_dealer_dealer)
add_cpp_single_bench(perf_dealer_router
  "${PERF_ROOT_DIR}/single/src/perf_dealer_router.cpp"
  run_pattern_dealer_router)
add_cpp_single_bench(perf_router_router
  "${PERF_ROOT_DIR}/single/src/perf_router_router.cpp"
  run_pattern_router_router)
add_cpp_single_bench(perf_router_router_poll
  "${PERF_ROOT_DIR}/single/src/perf_router_router_poll.cpp"
  run_pattern_router_router_poll)
add_cpp_single_bench(perf_gateway
  "${PERF_ROOT_DIR}/single/src/perf_gateway.cpp"
  run_pattern_gateway)
add_cpp_single_bench(perf_spot
  "${PERF_ROOT_DIR}/single/src/perf_spot.cpp"
  run_pattern_spot)

# Multi server suite (9 binaries)
add_cpp_multi_server_bench(comp_src_dealer_dealer_server
  "${PERF_ROOT_DIR}/multi/src/perf_dealer_dealer_server.cpp"
  perf_dealer_dealer_server)
add_cpp_multi_server_bench(comp_src_dealer_router_server
  "${PERF_ROOT_DIR}/multi/src/perf_dealer_router_server.cpp"
  perf_dealer_router_server)
add_cpp_multi_server_bench(comp_src_router_router_server
  "${PERF_ROOT_DIR}/multi/src/perf_router_router_server.cpp"
  perf_router_router_server)
add_cpp_multi_server_bench(comp_src_pubsub_server
  "${PERF_ROOT_DIR}/multi/src/perf_pubsub_server.cpp"
  perf_pubsub_server)
add_cpp_multi_server_bench(comp_src_gateway_server
  "${PERF_ROOT_DIR}/multi/src/perf_gateway_server.cpp"
  perf_gateway_server)
add_cpp_multi_server_bench(comp_src_spot_server
  "${PERF_ROOT_DIR}/multi/src/perf_spot_server.cpp"
  perf_spot_server)
add_cpp_multi_server_bench(comp_src_stream_server
  "${PERF_ROOT_DIR}/multi/src/perf_stream_server.cpp"
  perf_stream_server)
add_cpp_multi_server_bench(comp_src_stream_callback_server
  "${PERF_ROOT_DIR}/multi/src/perf_stream_callback_server.cpp"
  perf_stream_callback_server)
add_cpp_multi_server_bench(comp_src_stream_len32be_server
  "${PERF_ROOT_DIR}/multi/src/perf_stream_len32be_server.cpp"
  perf_stream_len32be_server)

# Multi client suite (6 binaries)
add_cpp_multi_client_bench(comp_src_dealer_dealer_client
  "${PERF_ROOT_DIR}/multi/src/perf_dealer_dealer_client.cpp"
  perf_dealer_dealer_client)
add_cpp_multi_client_bench(comp_src_dealer_router_client
  "${PERF_ROOT_DIR}/multi/src/perf_dealer_router_client.cpp"
  perf_dealer_router_client)
add_cpp_multi_client_bench(comp_src_router_router_client
  "${PERF_ROOT_DIR}/multi/src/perf_router_router_client.cpp"
  perf_router_router_client)
add_cpp_multi_client_bench(comp_src_pubsub_client
  "${PERF_ROOT_DIR}/multi/src/perf_pubsub_client.cpp"
  perf_pubsub_client)
add_cpp_multi_client_bench(comp_src_gateway_client
  "${PERF_ROOT_DIR}/multi/src/perf_gateway_client.cpp"
  perf_gateway_client)
add_cpp_multi_client_bench(comp_src_spot_client
  "${PERF_ROOT_DIR}/multi/src/perf_spot_client.cpp"
  perf_spot_client)
