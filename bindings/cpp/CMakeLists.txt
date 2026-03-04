cmake_minimum_required(VERSION 3.10)
project(zlink_cpp LANGUAGES CXX)

set(ZLINK_CORE_DIR "${CMAKE_SOURCE_DIR}/core" CACHE PATH "Path to zlink core")

add_library(zlink-cpp INTERFACE)
target_include_directories(zlink-cpp INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/include
  ${ZLINK_CORE_DIR}/include
)

target_compile_features(zlink-cpp INTERFACE cxx_std_11)

find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
  add_custom_target(zlink_cpp_api_docs
    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/Doxyfile
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    COMMENT "Generating zlink C++ binding API reference"
    VERBATIM
  )
endif()

if(NOT DEFINED ZLINK_CPP_BUILD_TESTS)
  option(ZLINK_CPP_BUILD_TESTS "Build C++ binding tests" OFF)
endif()

if(ZLINK_CPP_BUILD_TESTS)
  if(NOT TARGET libzlink)
    message(FATAL_ERROR "libzlink target not found. Build core first or add_subdirectory(core)")
  endif()

  function(add_cpp_binding_test test_name source_file)
    add_executable(${test_name} ${source_file})
    target_link_libraries(${test_name} PRIVATE libzlink zlink-cpp)
    if(MSVC)
      target_compile_options(${test_name} PRIVATE /UNDEBUG)
    else()
      target_compile_options(${test_name} PRIVATE -UNDEBUG)
    endif()
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 120)
    if(WIN32)
      set_tests_properties(${test_name}
        PROPERTIES
          ENVIRONMENT "PATH=$<TARGET_FILE_DIR:libzlink>;$ENV{PATH}")
    endif()
  endfunction()

  add_cpp_binding_test(test_cpp_core_connect_resolve tests/test_cpp_core_connect_resolve.cpp)
  add_cpp_binding_test(test_cpp_core_ctx_destroy tests/test_cpp_core_ctx_destroy.cpp)
  add_cpp_binding_test(test_cpp_core_ctx_options tests/test_cpp_core_ctx_options.cpp)
  add_cpp_binding_test(test_cpp_core_socket_null tests/test_cpp_core_socket_null.cpp)
  add_cpp_binding_test(test_cpp_core_ancillaries tests/test_cpp_core_ancillaries.cpp)
  add_cpp_binding_test(test_cpp_core_capabilities tests/test_cpp_core_capabilities.cpp)
  add_cpp_binding_test(test_cpp_core_system tests/test_cpp_core_system.cpp)
  add_cpp_binding_test(test_cpp_core_many_sockets tests/test_cpp_core_many_sockets.cpp)
  add_cpp_binding_test(test_cpp_core_heartbeats tests/test_cpp_core_heartbeats.cpp)
  add_cpp_binding_test(test_cpp_core_issue_566 tests/test_cpp_core_issue_566.cpp)
  add_cpp_binding_test(test_cpp_core_shutdown_stress tests/test_cpp_core_shutdown_stress.cpp)
  add_cpp_binding_test(test_cpp_core_proxy tests/test_cpp_core_proxy.cpp)
  add_cpp_binding_test(test_cpp_core_pair_inproc tests/test_cpp_core_pair_inproc.cpp)
  add_cpp_binding_test(test_cpp_core_pair_tcp tests/test_cpp_core_pair_tcp.cpp)
  add_cpp_binding_test(test_cpp_core_pair_ipc tests/test_cpp_core_pair_ipc.cpp)
  add_cpp_binding_test(test_cpp_core_pair_send_blocking_wakeup tests/test_cpp_core_pair_send_blocking_wakeup.cpp)
  add_cpp_binding_test(test_cpp_core_pubsub tests/test_cpp_core_pubsub.cpp)
  add_cpp_binding_test(test_cpp_core_sub_forward tests/test_cpp_core_sub_forward.cpp)
  add_cpp_binding_test(test_cpp_core_abstract_ipc tests/test_cpp_core_abstract_ipc.cpp)
  add_cpp_binding_test(test_cpp_core_bind_after_connect_tcp tests/test_cpp_core_bind_after_connect_tcp.cpp)
  add_cpp_binding_test(test_cpp_core_bind_src_address tests/test_cpp_core_bind_src_address.cpp)
  add_cpp_binding_test(test_cpp_core_getsockopt_memset tests/test_cpp_core_getsockopt_memset.cpp)
  add_cpp_binding_test(test_cpp_core_diffserv tests/test_cpp_core_diffserv.cpp)
  add_cpp_binding_test(test_cpp_core_ipc_wildcard tests/test_cpp_core_ipc_wildcard.cpp)
  add_cpp_binding_test(test_cpp_core_reconnect_ivl tests/test_cpp_core_reconnect_ivl.cpp)
  add_cpp_binding_test(test_cpp_core_reconnect_options tests/test_cpp_core_reconnect_options.cpp)
  add_cpp_binding_test(test_cpp_core_use_fd tests/test_cpp_core_use_fd.cpp)
  add_cpp_binding_test(test_cpp_core_last_endpoint tests/test_cpp_core_last_endpoint.cpp)
  add_cpp_binding_test(test_cpp_core_immediate tests/test_cpp_core_immediate.cpp)
  add_cpp_binding_test(test_cpp_core_timeo tests/test_cpp_core_timeo.cpp)
  add_cpp_binding_test(test_cpp_core_probe_router tests/test_cpp_core_probe_router.cpp)
  add_cpp_binding_test(test_cpp_core_pub_invert_matching tests/test_cpp_core_pub_invert_matching.cpp)
  add_cpp_binding_test(test_cpp_core_pubsub_filter_xpub tests/test_cpp_core_pubsub_filter_xpub.cpp)
  add_cpp_binding_test(test_cpp_core_disconnect_inproc tests/test_cpp_core_disconnect_inproc.cpp)
  add_cpp_binding_test(test_cpp_core_msg_flags tests/test_cpp_core_msg_flags.cpp)
  add_cpp_binding_test(test_cpp_core_msg_ffn tests/test_cpp_core_msg_ffn.cpp)
  add_cpp_binding_test(test_cpp_core_connect_rid tests/test_cpp_core_connect_rid.cpp)
  add_cpp_binding_test(test_cpp_core_connect_rid_string_alias tests/test_cpp_core_connect_rid_string_alias.cpp)
  add_cpp_binding_test(test_cpp_core_router_auto_id_format tests/test_cpp_core_router_auto_id_format.cpp)
  add_cpp_binding_test(test_cpp_core_router_mandatory tests/test_cpp_core_router_mandatory.cpp)
  add_cpp_binding_test(test_cpp_core_router_multiple_dealers tests/test_cpp_core_router_multiple_dealers.cpp)
  add_cpp_binding_test(test_cpp_core_spec_router tests/test_cpp_core_spec_router.cpp)
  add_cpp_binding_test(test_cpp_core_router_handover tests/test_cpp_core_router_handover.cpp)
  add_cpp_binding_test(test_cpp_core_router_mandatory_hwm tests/test_cpp_core_router_mandatory_hwm.cpp)
  add_cpp_binding_test(test_cpp_core_hwm_pubsub tests/test_cpp_core_hwm_pubsub.cpp)
  add_cpp_binding_test(test_cpp_core_transport_matrix tests/test_cpp_core_transport_matrix.cpp)
  add_cpp_binding_test(test_cpp_core_xpub_nodrop tests/test_cpp_core_xpub_nodrop.cpp)
  add_cpp_binding_test(test_cpp_core_xpub_verbose tests/test_cpp_core_xpub_verbose.cpp)
  add_cpp_binding_test(test_cpp_core_xpub_manual tests/test_cpp_core_xpub_manual.cpp)
  add_cpp_binding_test(test_cpp_core_xpub_welcome_msg tests/test_cpp_core_xpub_welcome_msg.cpp)
  add_cpp_binding_test(test_cpp_core_xpub_topic tests/test_cpp_core_xpub_topic.cpp)
  add_cpp_binding_test(test_cpp_core_zmp_metadata tests/test_cpp_core_zmp_metadata.cpp)
  add_cpp_binding_test(test_cpp_core_zmp_ws_wss tests/test_cpp_core_zmp_ws_wss.cpp)
  add_cpp_binding_test(test_cpp_core_monitor_enhanced tests/test_cpp_core_monitor_enhanced.cpp)
  add_cpp_binding_test(test_cpp_core_stream_fastpath tests/test_cpp_core_stream_fastpath.cpp)
  add_cpp_binding_test(test_cpp_core_stream_socket tests/test_cpp_core_stream_socket.cpp)
  add_cpp_binding_test(test_cpp_core_stream_send_blocking_wakeup tests/test_cpp_core_stream_send_blocking_wakeup.cpp)
  add_cpp_binding_test(test_cpp_core_stream_routing_id_size tests/test_cpp_core_stream_routing_id_size.cpp)
  add_cpp_binding_test(test_cpp_core_spot_pubsub_basic tests/test_cpp_core_spot_pubsub_basic.cpp)
  add_cpp_binding_test(test_cpp_core_spot_pubsub_scenario tests/test_cpp_core_spot_pubsub_scenario.cpp)
  add_cpp_binding_test(test_cpp_core_spot_send_blocking_wakeup tests/test_cpp_core_spot_send_blocking_wakeup.cpp)
  add_cpp_binding_test(test_cpp_core_gateway tests/test_cpp_core_gateway.cpp)
  add_cpp_binding_test(test_cpp_core_service_discovery tests/test_cpp_core_service_discovery.cpp)
  add_cpp_binding_test(test_cpp_core_gateway_handover tests/test_cpp_core_gateway_handover.cpp)
  add_cpp_binding_test(test_cpp_core_socket_options_typed tests/test_cpp_core_socket_options_typed.cpp)
  add_cpp_binding_test(test_cpp_core_message_send_ownership tests/test_cpp_core_message_send_ownership.cpp)
endif()

option(ZLINK_CPP_BUILD_BENCHMARKS "Build C++ binding benchmarks" OFF)

if(ZLINK_CPP_BUILD_BENCHMARKS)
  if(NOT TARGET libzlink)
    message(FATAL_ERROR "libzlink target not found. Build core first or add_subdirectory(core)")
  endif()

  find_package(Threads REQUIRED)
  add_executable(cpp_pair_bench
    perf/single/perf_main.cpp
    perf/single/perf_pair.cpp
    perf/single/perf_pubsub.cpp
    perf/single/perf_dealer_dealer.cpp
    perf/single/perf_dealer_router.cpp
    perf/single/perf_router_router.cpp
    perf/single/perf_router_router_poll.cpp
    perf/single/perf_stream.cpp
    perf/single/perf_stream_callback.cpp
    perf/single/perf_gateway.cpp
    perf/single/perf_spot.cpp
  )
  target_link_libraries(cpp_pair_bench PRIVATE libzlink zlink-cpp Threads::Threads)
  target_compile_features(cpp_pair_bench PRIVATE cxx_std_17)
endif()
