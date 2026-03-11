# CMake build script for Zlink tests
cmake_minimum_required(VERSION 3.10)

# On Windows: solution file will be called tests.sln
project(tests)

set(TEST_PTY_WRAPPER "${CMAKE_CURRENT_LIST_DIR}/run_under_pty.sh")

set(STREAM_SCENARIO_ROOT "${CMAKE_SOURCE_DIR}/core/bench/with_stream/stacks")
if(NOT EXISTS "${STREAM_SCENARIO_ROOT}")
  set(STREAM_SCENARIO_ROOT
      "${CMAKE_SOURCE_DIR}/core/bench/benchwithstreamcompare/stacks")
endif()

set(tests
  test_ancillaries
  test_monitor_with_handler
  test_socket_with_handler
  test_connect_resolve
  discovery/test_gateway
  discovery/test_gateway_with_handler
  discovery/test_gateway_handover
  test_capabilities
  test_socket_null
  routing-id/test_stream_routing_id_size
  discovery/test_service_discovery
  discovery/test_service_introspection
  spot/test_spot_pubsub_scenario
  spot/test_spot_service_introspection
  ${STREAM_SCENARIO_ROOT}/client/test_scenario_stream_client
  ${STREAM_SCENARIO_ROOT}/zlink/test_scenario_stream_zlink
  ${STREAM_SCENARIO_ROOT}/zlink/test_scenario_stream_zlink_len32be
  ${STREAM_SCENARIO_ROOT}/asio/test_scenario_stream_asio)

set(ROUTER_ROUTER_ZLINK_CONNECT_TEST
    "scenario/router_router/zlink-connect/test_scenario_router_router_zlink_connect")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/${ROUTER_ROUTER_ZLINK_CONNECT_TEST}.cpp")
  list(APPEND tests ${ROUTER_ROUTER_ZLINK_CONNECT_TEST})
endif()

set(STREAM_SCENARIO_ZMQ_DIST_ROOT_DEFAULT
    "${STREAM_SCENARIO_ROOT}/zmq/libzmq_dist")

if(NOT DEFINED STREAM_SCENARIO_ZMQ_DIST_ROOT)
  set(
    STREAM_SCENARIO_ZMQ_DIST_ROOT
    "${STREAM_SCENARIO_ZMQ_DIST_ROOT_DEFAULT}"
    CACHE PATH "Bundled libzmq distribution root for stream scenario tests")
elseif(NOT EXISTS "${STREAM_SCENARIO_ZMQ_DIST_ROOT}"
       AND EXISTS "${STREAM_SCENARIO_ZMQ_DIST_ROOT_DEFAULT}")
  set(
    STREAM_SCENARIO_ZMQ_DIST_ROOT
    "${STREAM_SCENARIO_ZMQ_DIST_ROOT_DEFAULT}"
    CACHE PATH "Bundled libzmq distribution root for stream scenario tests"
          FORCE)
endif()

set(STREAM_SCENARIO_ZMQ_ARCH "")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|X86_64|amd64|AMD64|x64)$")
  set(STREAM_SCENARIO_ZMQ_ARCH "x64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|AARCH64|arm64|ARM64)$")
  set(STREAM_SCENARIO_ZMQ_ARCH "arm64")
endif()

set(STREAM_SCENARIO_ZMQ_PLATFORM "")
if(WIN32)
  set(STREAM_SCENARIO_ZMQ_PLATFORM "windows")
elseif(APPLE)
  set(STREAM_SCENARIO_ZMQ_PLATFORM "macos")
elseif(CMAKE_SYSTEM_NAME MATCHES "Linux")
  set(STREAM_SCENARIO_ZMQ_PLATFORM "linux")
endif()

set(STREAM_SCENARIO_ZMQ_ROOT "")
if(STREAM_SCENARIO_ZMQ_PLATFORM AND STREAM_SCENARIO_ZMQ_ARCH)
  set(
    STREAM_SCENARIO_ZMQ_ROOT
    "${STREAM_SCENARIO_ZMQ_DIST_ROOT}/${STREAM_SCENARIO_ZMQ_PLATFORM}-${STREAM_SCENARIO_ZMQ_ARCH}")
endif()

set(STREAM_SCENARIO_ZMQ_LIBRARY "")
set(STREAM_SCENARIO_ZMQ_RUNTIME_DIR "")
set(STREAM_SCENARIO_ZMQ_INCLUDE_DIR "")
if(STREAM_SCENARIO_ZMQ_ROOT)
  if(WIN32)
    file(
      GLOB
      STREAM_SCENARIO_ZMQ_LIBRARY
      "${STREAM_SCENARIO_ZMQ_ROOT}/lib/*.lib")
    set(STREAM_SCENARIO_ZMQ_RUNTIME_DIR "${STREAM_SCENARIO_ZMQ_ROOT}/bin")
  else()
    file(
      GLOB
      STREAM_SCENARIO_ZMQ_LIBRARY
      "${STREAM_SCENARIO_ZMQ_ROOT}/lib/libzmq.so"
      "${STREAM_SCENARIO_ZMQ_ROOT}/lib/libzmq.so.*"
      "${STREAM_SCENARIO_ZMQ_ROOT}/lib/libzmq.dylib")
    set(STREAM_SCENARIO_ZMQ_RUNTIME_DIR "${STREAM_SCENARIO_ZMQ_ROOT}/lib")
  endif()
  set(STREAM_SCENARIO_ZMQ_INCLUDE_DIR "${STREAM_SCENARIO_ZMQ_ROOT}/include")
endif()

set(STREAM_SCENARIO_ZMQ_LIBRARY_FILE "")
if(STREAM_SCENARIO_ZMQ_LIBRARY)
  list(GET STREAM_SCENARIO_ZMQ_LIBRARY 0 STREAM_SCENARIO_ZMQ_LIBRARY_FILE)
endif()

if(EXISTS "${STREAM_SCENARIO_ZMQ_INCLUDE_DIR}/zmq.h"
   AND STREAM_SCENARIO_ZMQ_LIBRARY_FILE)
  list(APPEND tests ${STREAM_SCENARIO_ROOT}/zmq/test_scenario_stream_zmq)
else()
  message(
    STATUS
      "stream scenario zmq test disabled: libzmq bundle not found under ${STREAM_SCENARIO_ZMQ_ROOT}")
endif()

if(NOT WIN32)
endif()

option(ENABLE_CAPSH "Run tests that require sudo and capsh (for cap_net_admin)" OFF)

if(ENABLE_CAPSH)
  find_program(CAPSH_PROGRAM NAMES capsh)

  if(CAPSH_PROGRAM)
    list(APPEND tests test_pair_tcp_cap_net_admin)
  else()
    message(STATUS "capsh not found, skipping tests that require CAP_NET_ADMIN")
  endif()
endif()

if(ZLINK_HAVE_IPC)
endif()

if(ZLINK_HAVE_OPENPGM)
  list(APPEND tests pgm/test_pgm_epgm)
endif()

# add location of platform.hpp for Windows builds
if(WIN32)
  add_definitions(-DZLINK_CUSTOM_PLATFORM_HPP)
  add_definitions(-D_WINSOCK_DEPRECATED_NO_WARNINGS)

  # Same name on 64bit systems
  link_libraries(ws2_32.lib)
endif()

add_library(
  unity STATIC
  "${CMAKE_SOURCE_DIR}/core/external/unity/unity.c" "${CMAKE_SOURCE_DIR}/core/external/unity/unity.h"
  "${CMAKE_SOURCE_DIR}/core/external/unity/unity_internals.h")
set_target_properties(unity PROPERTIES PUBLIC_HEADER "${CMAKE_SOURCE_DIR}/core/external/unity/unity.h")
target_compile_definitions(unity PUBLIC "UNITY_USE_COMMAND_LINE_ARGS" "UNITY_EXCLUDE_FLOAT")
target_include_directories(unity PUBLIC "${CMAKE_SOURCE_DIR}/core/external/unity")

set(TESTUTIL_SOURCES
  testutil.cpp
  testutil.hpp
  testutil_monitoring.cpp
  testutil_monitoring.hpp
  testutil_unity.cpp
  testutil_unity.hpp)

if(BUILD_STATIC)
  add_library(testutil-static STATIC ${TESTUTIL_SOURCES})
  target_link_libraries(testutil-static libzlink-static ${OPTIONAL_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} unity)
endif()

if(BUILD_SHARED)
  add_library(testutil STATIC ${TESTUTIL_SOURCES})
  target_link_libraries(testutil libzlink ${OPTIONAL_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} unity)
endif()

if(BUILD_STATIC AND NOT BUILD_SHARED)
  # use testutil-static for both tests and unit tests
  set(TESTUTIL_LIB testutil-static)
else()
  # use testutil for tests and testutil-static for unit tests
  set(TESTUTIL_LIB testutil)
endif()

if(MSVC_VERSION LESS 1700)
  set_source_files_properties("${CMAKE_SOURCE_DIR}/core/external/unity/unity.c" PROPERTIES LANGUAGE CXX)
endif()

if(MSVC_VERSION LESS 1600)
  target_compile_definitions(unity PUBLIC "UNITY_EXCLUDE_STDINT_H")
endif()

# add include dirs for all targets
# Note: Using CMAKE_SOURCE_DIR instead of Zlink_SOURCE_DIR since project name is now zlink
include_directories("${CMAKE_SOURCE_DIR}/core/include" "${CMAKE_BINARY_DIR}")
include_directories("${CMAKE_CURRENT_LIST_DIR}")

# Add Boost include directory for ASIO tests (ASIO is mandatory)
include_directories("${BUNDLED_BOOST_INCLUDE_DIR}")

if(WIN32)
  add_definitions(-D_CRT_NONSTDC_NO_DEPRECATE)
endif()

# Does not work, times out every time
if(WIN32)
  list(REMOVE_ITEM tests test_many_sockets)
endif()

foreach(test_source ${tests})
  get_filename_component(test ${test_source} NAME)
  # target_sources not supported before CMake 3.1
  add_executable(${test} ${test_source}.cpp)

  target_link_libraries(${test} ${TESTUTIL_LIB})

  if(WIN32)
    # This is the output for Debug dynamic builds on Visual Studio 6.0 You should provide the correct directory, don't
    # know how to do it automatically
    find_path(LIBZLINK_PATH "libzlink.lib" PATHS "../bin/Win32/Debug/v120/dynamic")

    if(NOT ${LIBZLINK_PATH} STREQUAL "LIBZLINK_PATH-NOTFOUND")
      set_target_properties(${test} PROPERTIES LINK_FLAGS "/LIBPATH:${LIBZLINK_PATH}")
    endif()
  else()
    # per-test directories not generated on OS X / Darwin
    if(NOT APPLE)
      link_directories(${test} PRIVATE "${Zlink_SOURCE_DIR}/../lib")
    endif()
  endif()

  if(RT_LIBRARY)
    target_link_libraries(${test} ${RT_LIBRARY})
  endif()

  if(CMAKE_SYSTEM_NAME MATCHES "QNX")
    target_link_libraries(${test} socket)
    target_link_libraries(${test} m)
  endif()

  if(WIN32)
    add_test(
      NAME ${test}
      WORKING_DIRECTORY ${LIBRARY_OUTPUT_PATH}
      COMMAND ${test})
  else()
    if(${test} MATCHES "_cap_net_admin")
      add_test(NAME ${test} COMMAND sh -c "sudo ${CAPSH_PROGRAM} --caps=cap_net_admin+eip -- -c $<TARGET_FILE:${test}>")
    else()
      add_test(NAME ${test} COMMAND ${test})
    endif()
  endif()

  set_tests_properties(${test} PROPERTIES TIMEOUT 10)
  set_tests_properties(${test} PROPERTIES SKIP_RETURN_CODE 77)

  # Automatically set library path for tests to find libzlink shared library
  if(WIN32)
    set_tests_properties(${test} PROPERTIES ENVIRONMENT "PATH=$<TARGET_FILE_DIR:libzlink>;$ENV{PATH}")
  elseif(APPLE)
    set_tests_properties(${test} PROPERTIES ENVIRONMENT "DYLD_LIBRARY_PATH=$<TARGET_FILE_DIR:libzlink>:$ENV{DYLD_LIBRARY_PATH}")
  else()
    set_tests_properties(${test} PROPERTIES ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:libzlink>:$ENV{LD_LIBRARY_PATH}")
  endif()

  if(QNX)
    install(TARGETS ${test} RUNTIME DESTINATION bin/)
  endif()
endforeach()

target_sources(
  test_scenario_stream_client
  PRIVATE ${STREAM_SCENARIO_ROOT}/client/stream_client_core.cpp)

if(TARGET test_scenario_stream_zmq)
  target_include_directories(
    test_scenario_stream_zmq PRIVATE ${STREAM_SCENARIO_ZMQ_INCLUDE_DIR})
  target_link_libraries(test_scenario_stream_zmq
                        ${STREAM_SCENARIO_ZMQ_LIBRARY_FILE})

  if(WIN32)
    set_tests_properties(
      test_scenario_stream_zmq
      PROPERTIES ENVIRONMENT
                 "PATH=${STREAM_SCENARIO_ZMQ_RUNTIME_DIR};$<TARGET_FILE_DIR:libzlink>;$ENV{PATH}")
  elseif(APPLE)
    set_tests_properties(
      test_scenario_stream_zmq
      PROPERTIES ENVIRONMENT
                 "DYLD_LIBRARY_PATH=${STREAM_SCENARIO_ZMQ_RUNTIME_DIR}:$<TARGET_FILE_DIR:libzlink>:$ENV{DYLD_LIBRARY_PATH}")
  else()
    set_tests_properties(
      test_scenario_stream_zmq
      PROPERTIES ENVIRONMENT
                 "LD_LIBRARY_PATH=${STREAM_SCENARIO_ZMQ_RUNTIME_DIR}:$<TARGET_FILE_DIR:libzlink>:$ENV{LD_LIBRARY_PATH}")
  endif()
endif()

# override timeout for these tests
if(TARGET test_heartbeats)
  set_tests_properties(test_heartbeats PROPERTIES TIMEOUT 60)
endif()
if(TARGET test_reconnect_ivl)
  set_tests_properties(test_reconnect_ivl PROPERTIES TIMEOUT 15)
endif()
if(TARGET test_issue_566)
  set_tests_properties(test_issue_566 PROPERTIES TIMEOUT 30)
endif()
if(TARGET test_transport_matrix)
  set_tests_properties(test_transport_matrix PROPERTIES TIMEOUT 30)
endif()
if(TARGET test_service_discovery)
  set_tests_properties(test_service_discovery PROPERTIES TIMEOUT 30)
endif()
if(TARGET test_service_introspection)
  set_tests_properties(
    test_service_introspection
    PROPERTIES
      TIMEOUT 30
      ENVIRONMENT
        "ZLINK_TEST_CASE=test_discovery_monitor_and_routing_id")

  add_test(
    NAME test_service_introspection_gateway_options
    COMMAND ${CMAKE_COMMAND} -E env
            ZLINK_TEST_CASE=test_gateway_receiver_routing_ids_and_options
            $<TARGET_FILE:test_service_introspection>)
  add_test(
    NAME test_service_introspection_gateway_monitors
    COMMAND ${CMAKE_COMMAND} -E env
            ZLINK_TEST_CASE=test_gateway_receiver_monitors_and_monitor_poller
            $<TARGET_FILE:test_service_introspection>)
  add_test(
    NAME test_service_introspection_topology_snapshot
    COMMAND ${CMAKE_COMMAND} -E env
            ZLINK_TEST_CASE=test_registry_topology_snapshot_and_remote_query
            $<TARGET_FILE:test_service_introspection>)
  add_test(
    NAME test_service_introspection_closed_event
    COMMAND ${CMAKE_COMMAND} -E env
            ZLINK_TEST_CASE=test_monitor_closed_event_on_service_destroy
            $<TARGET_FILE:test_service_introspection>)
  add_test(
    NAME test_service_introspection_transport_restriction
    COMMAND ${CMAKE_COMMAND} -E env
            ZLINK_TEST_CASE=test_discovery_registry_transport_restriction
            $<TARGET_FILE:test_service_introspection>)
  add_test(
    NAME test_service_introspection_registry_peer_sync
    COMMAND ${CMAKE_COMMAND} -E env
            ZLINK_TEST_CASE=test_registry_peer_transport_restriction_and_tcp_sync
            $<TARGET_FILE:test_service_introspection>)
  add_test(
    NAME test_service_introspection_topology_reports
    COMMAND ${CMAKE_COMMAND} -E env
            ZLINK_TEST_CASE=test_registry_topology_reports_discovery_and_gateway
            $<TARGET_FILE:test_service_introspection>)

  set_tests_properties(
    test_service_introspection_gateway_options
    test_service_introspection_gateway_monitors
    test_service_introspection_topology_snapshot
    test_service_introspection_closed_event
    test_service_introspection_transport_restriction
    test_service_introspection_registry_peer_sync
    test_service_introspection_topology_reports
    PROPERTIES
      TIMEOUT 30
      SKIP_RETURN_CODE 77)
endif()

function(add_env_case_test test_name target_name test_case)
  add_test(
    NAME ${test_name}
    COMMAND ${CMAKE_COMMAND} -E env
            ZLINK_TEST_CASE=${test_case}
            $<TARGET_FILE:${target_name}>)
endfunction()

if(TARGET test_spot_service_introspection)
  set_tests_properties(
    test_spot_service_introspection
    PROPERTIES
      TIMEOUT 30
      DISABLED TRUE
      ENVIRONMENT
        "ZLINK_TEST_CASE=test_spot_faulted_node_apis_fail_with_efsm")

  add_env_case_test(
    test_spot_service_introspection_pub_sub_options
    test_spot_service_introspection
    test_spot_pub_sub_options_and_routing_ids)
  add_env_case_test(
    test_spot_service_introspection_monitors
    test_spot_service_introspection
    test_spot_monitors_and_monitor_poller)
  add_env_case_test(
    test_spot_service_introspection_explicit_handles
    test_spot_service_introspection
    test_spot_node_direct_apis_and_explicit_handles_interop)
  add_env_case_test(
    test_spot_service_introspection_topology_summary
    test_spot_service_introspection
    test_spot_topology_summary_lifecycle)
  add_env_case_test(
    test_spot_service_introspection_register_null
    test_spot_service_introspection
    test_spot_register_null_derivation_and_wildcard_rejection)
  add_env_case_test(
    test_spot_service_introspection_tls_lock
    test_spot_service_introspection
    test_spot_tls_settings_lock_after_bind_connect_and_register)
  add_env_case_test(
    test_spot_service_introspection_late_connect
    test_spot_service_introspection
    test_spot_late_connect_replays_existing_subscription)
  add_env_case_test(
    test_spot_service_introspection_faulted
    test_spot_service_introspection
    test_spot_faulted_node_apis_fail_with_efsm)

  set_tests_properties(
    test_spot_service_introspection_pub_sub_options
    test_spot_service_introspection_monitors
    test_spot_service_introspection_explicit_handles
    test_spot_service_introspection_topology_summary
    test_spot_service_introspection_register_null
    test_spot_service_introspection_tls_lock
    test_spot_service_introspection_late_connect
    test_spot_service_introspection_faulted
    PROPERTIES
      TIMEOUT 60
      SKIP_RETURN_CODE 77)
endif()
if(TARGET test_gateway)
  set_tests_properties(test_gateway PROPERTIES TIMEOUT 180)
endif()
if(TARGET test_spot_pubsub_scenario)
  set_tests_properties(
    test_spot_pubsub_scenario
    PROPERTIES
      TIMEOUT 120
      DISABLED TRUE
      ENVIRONMENT "ZLINK_TEST_CASE=test_spot_peer_tcp"
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

  add_test(
    NAME test_spot_pubsub_scenario_peer_tcp
    COMMAND ${CMAKE_COMMAND} -E env
            ZLINK_TEST_CASE=test_spot_peer_tcp
            $<TARGET_FILE:test_spot_pubsub_scenario>)
  add_env_case_test(
    test_spot_pubsub_scenario_peer_ipc
    test_spot_pubsub_scenario
    test_spot_peer_ipc)
  add_env_case_test(
    test_spot_pubsub_scenario_peer_ws
    test_spot_pubsub_scenario
    test_spot_peer_ws)
  add_test(
    NAME test_spot_pubsub_scenario_peer_tls
    COMMAND ${CMAKE_COMMAND} -E env
            ZLINK_TEST_CASE=test_spot_peer_tls
            $<TARGET_FILE:test_spot_pubsub_scenario>)
  add_env_case_test(
    test_spot_pubsub_scenario_peer_wss
    test_spot_pubsub_scenario
    test_spot_peer_wss)
  add_test(
    NAME test_spot_pubsub_scenario_multi_publisher
    COMMAND ${CMAKE_COMMAND} -E env
            ZLINK_TEST_CASE=test_spot_multi_publisher
            $<TARGET_FILE:test_spot_pubsub_scenario>)
  add_test(
    NAME test_spot_pubsub_scenario_node_child_interop
    COMMAND ${CMAKE_COMMAND} -E env
            ZLINK_TEST_CASE=test_spot_node_direct_local_and_child_interop
            $<TARGET_FILE:test_spot_pubsub_scenario>)
  add_test(
    NAME test_spot_pubsub_scenario_node_remote_mesh
    COMMAND ${CMAKE_COMMAND} -E env
            ZLINK_TEST_CASE=test_spot_node_direct_remote_peer_mesh
            $<TARGET_FILE:test_spot_pubsub_scenario>)
  add_test(
    NAME test_spot_pubsub_scenario_sub_option_inheritance
    COMMAND ${CMAKE_COMMAND} -E env
            ZLINK_TEST_CASE=test_spot_node_direct_sub_option_inheritance_and_handler_conflict
            $<TARGET_FILE:test_spot_pubsub_scenario>)
  add_test(
    NAME test_spot_pubsub_scenario_first_publish_race
    COMMAND ${CMAKE_COMMAND} -E env
            ZLINK_TEST_CASE=test_spot_node_direct_first_publish_race
            $<TARGET_FILE:test_spot_pubsub_scenario>)
  add_test(
    NAME test_spot_pubsub_scenario_sub_handler_basic
    COMMAND ${CMAKE_COMMAND} -E env
            ZLINK_TEST_CASE=test_spot_sub_handler_basic
            $<TARGET_FILE:test_spot_pubsub_scenario>)
  add_test(
    NAME test_spot_pubsub_scenario_source_rid
    COMMAND ${CMAKE_COMMAND} -E env
            ZLINK_TEST_CASE=test_spot_facade_handler_receives_source_rid
            $<TARGET_FILE:test_spot_pubsub_scenario>)
  add_test(
    NAME test_spot_pubsub_scenario_handler_replace_barrier
    COMMAND ${CMAKE_COMMAND} -E env
            ZLINK_TEST_CASE=test_spot_sub_handler_replace_barrier
            $<TARGET_FILE:test_spot_pubsub_scenario>)
  add_test(
    NAME test_spot_pubsub_scenario_handler_replace_inside
    COMMAND ${CMAKE_COMMAND} -E env
            ZLINK_TEST_CASE=test_spot_sub_handler_replace_inside_callback
            $<TARGET_FILE:test_spot_pubsub_scenario>)
  add_test(
    NAME test_spot_pubsub_scenario_async_local_delivery
    COMMAND ${CMAKE_COMMAND} -E env
            ZLINK_TEST_CASE=test_spot_pub_async_mode_local_delivery
            $<TARGET_FILE:test_spot_pubsub_scenario>)
  add_test(
    NAME test_spot_pubsub_scenario_async_setsockopt
    COMMAND ${CMAKE_COMMAND} -E env
            ZLINK_TEST_CASE=test_spot_pub_async_setsockopt_validation
            $<TARGET_FILE:test_spot_pubsub_scenario>)
  add_test(
    NAME test_spot_pubsub_scenario_async_eagain
    COMMAND ${CMAKE_COMMAND} -E env
            ZLINK_TEST_CASE=test_spot_pub_async_queue_full_eagain
            $<TARGET_FILE:test_spot_pubsub_scenario>)
  add_test(
    NAME test_spot_pubsub_scenario_async_drop
    COMMAND ${CMAKE_COMMAND} -E env
            ZLINK_TEST_CASE=test_spot_pub_async_queue_full_drop
            $<TARGET_FILE:test_spot_pubsub_scenario>)
  add_test(
    NAME test_spot_pubsub_scenario_scale
    COMMAND ${CMAKE_COMMAND} -E env
            ZLINK_TEST_CASE=test_spot_mmorpg_zone_adjacency_scale
            $<TARGET_FILE:test_spot_pubsub_scenario>)
  add_test(
    NAME test_spot_pubsub_scenario_scale_multi_node
    COMMAND ${CMAKE_COMMAND} -E env
            ZLINK_TEST_CASE=test_spot_mmorpg_zone_adjacency_scale_multi_node_discovery
            $<TARGET_FILE:test_spot_pubsub_scenario>)

  set_tests_properties(
    test_spot_pubsub_scenario_multi_publisher
    test_spot_pubsub_scenario_node_child_interop
    test_spot_pubsub_scenario_node_remote_mesh
    test_spot_pubsub_scenario_sub_option_inheritance
    test_spot_pubsub_scenario_first_publish_race
    test_spot_pubsub_scenario_sub_handler_basic
    test_spot_pubsub_scenario_source_rid
    test_spot_pubsub_scenario_handler_replace_barrier
    test_spot_pubsub_scenario_handler_replace_inside
    test_spot_pubsub_scenario_async_local_delivery
    test_spot_pubsub_scenario_async_setsockopt
    test_spot_pubsub_scenario_async_eagain
    test_spot_pubsub_scenario_async_drop
    test_spot_pubsub_scenario_scale
    test_spot_pubsub_scenario_scale_multi_node
    PROPERTIES
      TIMEOUT 60
      SKIP_RETURN_CODE 77
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

  set_tests_properties(
    test_spot_pubsub_scenario_peer_tcp
    test_spot_pubsub_scenario_peer_ipc
    test_spot_pubsub_scenario_peer_ws
    test_spot_pubsub_scenario_peer_tls
    test_spot_pubsub_scenario_peer_wss
    PROPERTIES
      TIMEOUT 90
      SKIP_RETURN_CODE 77
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

  set_tests_properties(test_spot_pubsub_scenario PROPERTIES LABELS "spot;spot_scenario")
  set_tests_properties(test_spot_pubsub_scenario PROPERTIES RUN_SERIAL TRUE)
endif()
if(TARGET test_gateway_handover)
  set_tests_properties(test_gateway_handover PROPERTIES TIMEOUT 180)
endif()
if(TARGET test_scenario_router_router_zlink_connect)
  set_tests_properties(test_scenario_router_router_zlink_connect PROPERTIES TIMEOUT 180)
  set_tests_properties(test_scenario_router_router_zlink_connect PROPERTIES LABELS "scenario;router_router;zlink_connect")
endif()
if(TARGET test_scenario_stream_client)
  set_tests_properties(test_scenario_stream_client PROPERTIES TIMEOUT 120)
  set_tests_properties(test_scenario_stream_client PROPERTIES LABELS "scenario;stream;client")
endif()
if(TARGET test_scenario_stream_zlink)
  set_tests_properties(test_scenario_stream_zlink PROPERTIES TIMEOUT 120)
  set_tests_properties(test_scenario_stream_zlink PROPERTIES LABELS "scenario;stream;zlink")
endif()
if(TARGET test_scenario_stream_zlink_len32be)
  set_tests_properties(test_scenario_stream_zlink_len32be PROPERTIES TIMEOUT 120)
  set_tests_properties(test_scenario_stream_zlink_len32be PROPERTIES LABELS "scenario;stream;zlink;len32be")
endif()
if(TARGET test_scenario_stream_asio)
  set_tests_properties(test_scenario_stream_asio PROPERTIES TIMEOUT 120)
  set_tests_properties(test_scenario_stream_asio PROPERTIES LABELS "scenario;stream;asio")
endif()
if(TARGET test_scenario_stream_zmq)
  set_tests_properties(test_scenario_stream_zmq PROPERTIES TIMEOUT 120)
  set_tests_properties(test_scenario_stream_zmq PROPERTIES LABELS "scenario;stream;zmq")
endif()

# Check whether all tests in the current folder are present
file(READ "${CMAKE_CURRENT_LIST_FILE}" CURRENT_LIST_FILE_CONTENT)
file(GLOB ALL_TEST_SOURCES "test_*.cpp" "*/test_*.cpp")

set(UNREGISTERED_TEST_ALLOWLIST
    test_asio_connect
    test_asio_poller
    test_asio_tcp
    test_asio_ws
    test_bind_ws_fuzzer
    test_bind_after_connect_tcp
    test_bind_src_address
    test_connect_fuzzer
    test_connect_rid
    test_connect_rid_string_alias
    test_connect_null_fuzzer
    test_ctx_options
    test_diffserv
    test_issue_566
    test_immediate
    test_ipc_wildcard
    test_last_endpoint
    test_many_sockets
    test_ctx_destroy
    test_disconnect_inproc
    test_asio_ssl
    test_abstract_ipc
    test_hwm_pubsub
    test_heartbeats
    test_getsockopt_memset
    test_monitor_enhanced
    test_msg_ffn
    test_msg_flags
    test_pair_inproc
    test_pair_ipc
    test_pair_send_blocking_wakeup
    test_pgm_smoke
    test_probe_router
    test_proxy
    test_pub_invert_matching
    test_pubsub_filter_xpub
    test_pubsub_inproc_alternating
    test_router_auto_id_format
    test_router_handover
    test_router_mandatory_hwm
    test_router_multiple_dealers
    test_reconnect_ivl
    test_reconnect_options
    test_socket_options_fuzzer
    test_spot_pubsub_basic
    test_spec_router
    test_stream_fastpath
    test_stream_send_blocking_wakeup
    test_stream_socket
    test_sub_forward
    test_system
    test_timeo
    test_use_fd
    test_shutdown_stress
    test_xpub_manual
    test_xpub_nodrop
    test_xpub_topic
    test_xpub_verbose
    test_xpub_welcome_msg
    test_zmp_metadata
    test_zmp_ws_wss
    test_z85_decode_fuzzer)

foreach(TEST_SOURCE ${ALL_TEST_SOURCES})
  get_filename_component(TESTNAME "${TEST_SOURCE}" NAME_WE)
  string(REGEX MATCH "${TESTNAME}" MATCH_TESTNAME "${CURRENT_LIST_FILE_CONTENT}")

  if(TESTNAME MATCHES "_fuzzer$")
    continue()
  endif()
  list(FIND UNREGISTERED_TEST_ALLOWLIST "${TESTNAME}" TEST_ALLOWLIST_INDEX)
  if(TEST_ALLOWLIST_INDEX GREATER -1)
    continue()
  endif()

  if(NOT MATCH_TESTNAME)
    message(AUTHOR_WARNING "Test '${TESTNAME}' is not known to CTest.")
  endif()
endforeach()
