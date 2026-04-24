# CMake build script for Zlink tests
cmake_minimum_required(VERSION 3.10)

# On Windows: solution file will be called tests.sln
project(tests)

set(TEST_PTY_WRAPPER "${CMAKE_CURRENT_LIST_DIR}/run_under_pty.sh")
set(REGISTERED_TESTS)
set(SPLIT_ONLY_TEST_TARGETS)
set(INTEGRATION_TEST_ROOT "integration")
set(E2E_TEST_ROOT "e2e")

set(STREAM_SCENARIO_ROOT
    "${ZLINK_REPO_ROOT}/bindings/c/bench/with_stream/stacks")
if(NOT EXISTS "${STREAM_SCENARIO_ROOT}")
  set(STREAM_SCENARIO_ROOT
      "${ZLINK_CORE_SOURCE_DIR}/bench/with_stream/stacks")
endif()
if(NOT EXISTS "${STREAM_SCENARIO_ROOT}")
  set(STREAM_SCENARIO_ROOT
      "${ZLINK_CORE_SOURCE_DIR}/bench/benchwithstreamcompare/stacks")
endif()

option(
  ZLINK_CORE_BUILD_BINDING_INTEGRATION_TESTS
  "Build core tests that depend on bindings/c perf or bench sources"
  OFF)

if(ZLINK_CORE_BUILD_BINDING_INTEGRATION_TESTS
   AND EXISTS "${ZLINK_REPO_ROOT}/bindings/c/CMakeLists.txt"
   AND NOT TARGET perf_spot)
  set(ZLINK_C_REGISTER_CTEST OFF CACHE BOOL
      "Do not let bindings/c register duplicate CTest entries from core/tests"
      FORCE)
  set(ZLINK_C_BUILD_BENCHMARKS ON CACHE BOOL
      "Build bindings/c perf helpers needed by core integration smoke tests"
      FORCE)
  set(ZLINK_C_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
  set(ZLINK_C_BUILD_BENCHES OFF CACHE BOOL "" FORCE)
  add_subdirectory("${ZLINK_REPO_ROOT}/bindings/c"
                   "${CMAKE_BINARY_DIR}/bindings-c")
endif()

set(tests
  ${INTEGRATION_TEST_ROOT}/test_ancillaries
  ${INTEGRATION_TEST_ROOT}/test_thread_safe_contract_policy
  ${INTEGRATION_TEST_ROOT}/monitoring/test_monitor_enhanced
  ${INTEGRATION_TEST_ROOT}/monitoring/test_monitor_socket_contract
  ${INTEGRATION_TEST_ROOT}/monitoring/test_monitor_perf_contract
  ${INTEGRATION_TEST_ROOT}/monitoring/test_multi_stream_server_reassembly
  ${INTEGRATION_TEST_ROOT}/test_socket_with_handler
  ${INTEGRATION_TEST_ROOT}/test_connect_resolve
  ${INTEGRATION_TEST_ROOT}/test_capabilities
  ${INTEGRATION_TEST_ROOT}/test_socket_null
  ${INTEGRATION_TEST_ROOT}/test_heartbeats
  ${INTEGRATION_TEST_ROOT}/test_multi_socket_contract_regressions
  ${INTEGRATION_TEST_ROOT}/test_backpressure_matrix
  ${INTEGRATION_TEST_ROOT}/test_backpressure_oneway_matrix
  ${INTEGRATION_TEST_ROOT}/test_public_inproc_multipart_send
  ${INTEGRATION_TEST_ROOT}/test_helper_send_part_basic
  ${INTEGRATION_TEST_ROOT}/test_helper_recv_part_basic
  ${INTEGRATION_TEST_ROOT}/test_helper_ownership
  ${INTEGRATION_TEST_ROOT}/test_helper_interleave
  ${INTEGRATION_TEST_ROOT}/test_helper_more_bad_send
  ${INTEGRATION_TEST_ROOT}/test_helper_request_sequence_failure
  ${INTEGRATION_TEST_ROOT}/test_pubsub
  ${INTEGRATION_TEST_ROOT}/test_reconnect_ivl
  ${INTEGRATION_TEST_ROOT}/test_reconnect_options
  ${INTEGRATION_TEST_ROOT}/test_pubsub_filter_xpub
  ${INTEGRATION_TEST_ROOT}/test_xpub_nodrop
  ${INTEGRATION_TEST_ROOT}/test_spot_dispatch_event
  ${INTEGRATION_TEST_ROOT}/test_spot_poller
  ${INTEGRATION_TEST_ROOT}/test_spot_runtime_activation
  ${INTEGRATION_TEST_ROOT}/test_discovery_resolve_spot
  ${INTEGRATION_TEST_ROOT}/test_peer_admission
  ${INTEGRATION_TEST_ROOT}/test_discovery_socket_auto_connect_policy
  ${INTEGRATION_TEST_ROOT}/test_timer_poller
  ${INTEGRATION_TEST_ROOT}/test_router_handover
  ${INTEGRATION_TEST_ROOT}/test_router_mandatory
  ${INTEGRATION_TEST_ROOT}/test_router_multiple_dealers
  ${INTEGRATION_TEST_ROOT}/test_zmp_request_reply
  ${INTEGRATION_TEST_ROOT}/test_zmp_request_reply_router_recv_surface
  ${INTEGRATION_TEST_ROOT}/test_router_concurrent_routed_recv
  ${INTEGRATION_TEST_ROOT}/test_router_mandatory_hwm
  ${INTEGRATION_TEST_ROOT}/test_stream_socket
  ${INTEGRATION_TEST_ROOT}/test_stream_threadsafe
  ${INTEGRATION_TEST_ROOT}/test_stream_send_blocking_wakeup
  ${INTEGRATION_TEST_ROOT}/test_stream_fastpath
  ${INTEGRATION_TEST_ROOT}/test_transport_matrix
  ${INTEGRATION_TEST_ROOT}/routing-id/test_router_auto_id_format
  ${INTEGRATION_TEST_ROOT}/routing-id/test_stream_routing_id_size
  ${E2E_TEST_ROOT}/spot/test_spot_pubsub_scenario
  ${E2E_TEST_ROOT}/spot/test_spot_service_introspection)

if(ZLINK_CORE_BUILD_BINDING_INTEGRATION_TESTS)
  list(APPEND tests
    ${INTEGRATION_TEST_ROOT}/monitoring/test_multi_dealer_dealer_benchmark_process
    ${INTEGRATION_TEST_ROOT}/monitoring/test_multi_pubsub_benchmark_process
    ${INTEGRATION_TEST_ROOT}/monitoring/test_multi_spot_benchmark_process
    ${INTEGRATION_TEST_ROOT}/monitoring/test_multi_stream_benchmark_process
    ${INTEGRATION_TEST_ROOT}/monitoring/test_single_pair_benchmark_process
    ${INTEGRATION_TEST_ROOT}/monitoring/test_single_spot_benchmark_process
    ${STREAM_SCENARIO_ROOT}/client/test_scenario_stream_client
    ${STREAM_SCENARIO_ROOT}/zlink/test_scenario_stream_zlink
    ${STREAM_SCENARIO_ROOT}/zlink_packet/test_scenario_stream_zlink_packet
    ${STREAM_SCENARIO_ROOT}/asio/test_scenario_stream_asio)
endif()

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

if(ZLINK_CORE_BUILD_BINDING_INTEGRATION_TESTS
   AND EXISTS "${STREAM_SCENARIO_ZMQ_INCLUDE_DIR}/zmq.h"
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
    list(APPEND tests ${INTEGRATION_TEST_ROOT}/test_pair_tcp_cap_net_admin)
  else()
    message(STATUS "capsh not found, skipping tests that require CAP_NET_ADMIN")
  endif()
endif()

if(ZLINK_HAVE_IPC)
endif()

if(ZLINK_HAVE_OPENPGM)
  list(APPEND tests ${INTEGRATION_TEST_ROOT}/pgm/test_pgm_epgm)
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
  "${ZLINK_CORE_SOURCE_DIR}/external/unity/unity.c" "${ZLINK_CORE_SOURCE_DIR}/external/unity/unity.h"
  "${ZLINK_CORE_SOURCE_DIR}/external/unity/unity_internals.h")
set_target_properties(unity PROPERTIES PUBLIC_HEADER "${ZLINK_CORE_SOURCE_DIR}/external/unity/unity.h")
target_compile_definitions(unity PUBLIC "UNITY_USE_COMMAND_LINE_ARGS" "UNITY_EXCLUDE_FLOAT")
target_include_directories(unity PUBLIC "${ZLINK_CORE_SOURCE_DIR}/external/unity")

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

if(BUILD_STATIC AND (NOT BUILD_SHARED OR WIN32))
  # Windows DLL builds do not export private helper symbols that testutil uses.
  # Keep all test executables on the static helper path when it is available.
  set(TESTUTIL_LIB testutil-static)
else()
  # use testutil for tests and testutil-static for unit tests
  set(TESTUTIL_LIB testutil)
endif()

if(BUILD_STATIC)
  add_subdirectory(unittest)
else()
  message(WARNING "Not building unit tests, since BUILD_STATIC is not enabled")
endif()

if(MSVC_VERSION LESS 1700)
  set_source_files_properties("${ZLINK_CORE_SOURCE_DIR}/external/unity/unity.c" PROPERTIES LANGUAGE CXX)
endif()

if(MSVC_VERSION LESS 1600)
  target_compile_definitions(unity PUBLIC "UNITY_EXCLUDE_STDINT_H")
endif()

# add include dirs for all targets
include_directories("${ZLINK_CORE_SOURCE_DIR}/include" "${CMAKE_BINARY_DIR}")
include_directories("${CMAKE_CURRENT_LIST_DIR}")

# Add Boost include directory for ASIO tests (ASIO is mandatory)
include_directories("${BUNDLED_BOOST_INCLUDE_DIR}")

add_executable(spot_node_10000_pubsub_bench
               bench/spot_node_10000_pubsub_bench.cpp)
target_link_libraries(spot_node_10000_pubsub_bench ${TESTUTIL_LIB})

add_executable(spot_node_10000_router_bench
               bench/spot_node_10000_router_bench.cpp)
target_link_libraries(spot_node_10000_router_bench ${TESTUTIL_LIB})

if(WIN32)
  add_definitions(-D_CRT_NONSTDC_NO_DEPRECATE)
endif()

# Does not work, times out every time
if(WIN32)
  list(REMOVE_ITEM tests ${INTEGRATION_TEST_ROOT}/test_many_sockets)
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

  if(NOT test IN_LIST SPLIT_ONLY_TEST_TARGETS)
    list(APPEND REGISTERED_TESTS ${test})

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
    if(${test} STREQUAL "test_zmp_request_reply")
      set_tests_properties(
        ${test}
        PROPERTIES
          TIMEOUT 180)
    endif()
    if(${test} STREQUAL "test_helper_request_sequence_failure")
      set_tests_properties(
        ${test}
        PROPERTIES
          TIMEOUT 120)
    endif()
    if(${test} STREQUAL "test_zmp_request_reply_router_recv_surface")
      set_tests_properties(
        ${test}
        PROPERTIES
          TIMEOUT 60)
    endif()
    if(${test} STREQUAL "test_multi_stream_benchmark_process")
      set_tests_properties(${test} PROPERTIES TIMEOUT 120)
    endif()
    set_tests_properties(${test} PROPERTIES SKIP_RETURN_CODE 77)

    # Automatically set library path for tests to find libzlink shared library
    if(WIN32)
      set_tests_properties(${test} PROPERTIES ENVIRONMENT "PATH=$<TARGET_FILE_DIR:libzlink>;$ENV{PATH}")
    elseif(APPLE)
      set_tests_properties(${test} PROPERTIES ENVIRONMENT "DYLD_LIBRARY_PATH=$<TARGET_FILE_DIR:libzlink>:$ENV{DYLD_LIBRARY_PATH}")
    else()
      set_tests_properties(${test} PROPERTIES ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:libzlink>:$ENV{LD_LIBRARY_PATH}")
    endif()
  endif()

  if(QNX)
    install(TARGETS ${test} RUNTIME DESTINATION bin/)
  endif()
endforeach()

if(TARGET test_thread_safe_contract_policy)
  target_compile_definitions(
    test_thread_safe_contract_policy
    PRIVATE
      TEST_ZLINK_HEADER_PATH="${ZLINK_CORE_SOURCE_DIR}/include/zlink.h"
      TEST_REPO_ROOT="${ZLINK_REPO_ROOT}")
endif()

if(TARGET test_scenario_stream_client)
  target_sources(
    test_scenario_stream_client
    PRIVATE ${STREAM_SCENARIO_ROOT}/client/stream_client_core.cpp)
endif()

if(TARGET zlink_c)
  if(TARGET test_scenario_stream_zlink)
    target_link_libraries(test_scenario_stream_zlink zlink_c)
  endif()
  if(TARGET test_scenario_stream_zlink_packet)
    target_link_libraries(test_scenario_stream_zlink_packet zlink_c)
  endif()
endif()

if(TARGET test_spot_pubsub_scenario)
  target_sources(
    test_spot_pubsub_scenario
    PRIVATE
      ${E2E_TEST_ROOT}/spot/spot_pubsub_scenario_shared.cpp
      ${E2E_TEST_ROOT}/spot/spot_pubsub_scenario_peer_cases.cpp
      ${E2E_TEST_ROOT}/spot/spot_pubsub_scenario_callback_cases.cpp
      ${E2E_TEST_ROOT}/spot/spot_pubsub_scenario_node_cases.cpp
      ${E2E_TEST_ROOT}/spot/spot_pubsub_scenario_discovery_cases.cpp)
endif()

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
if(TARGET test_backpressure_matrix)
  set_tests_properties(
    test_backpressure_matrix
    PROPERTIES
      TIMEOUT 360
      RESOURCE_LOCK "${NETWORK_INTEGRATION_RESOURCE_LOCK}")
endif()
if(TARGET test_backpressure_oneway_matrix)
  set_tests_properties(
    test_backpressure_oneway_matrix
    PROPERTIES
      TIMEOUT 180
      RESOURCE_LOCK "${NETWORK_INTEGRATION_RESOURCE_LOCK}")
endif()
if(TARGET test_zmp_request_reply)
  set_tests_properties(
    test_zmp_request_reply
    PROPERTIES
      TIMEOUT 180
      RESOURCE_LOCK "${NETWORK_INTEGRATION_RESOURCE_LOCK}")
endif()
if(TARGET test_zmp_request_reply_router_recv_surface)
  set_tests_properties(
    test_zmp_request_reply_router_recv_surface
    PROPERTIES
      TIMEOUT 60
      RESOURCE_LOCK "${NETWORK_INTEGRATION_RESOURCE_LOCK}")
endif()
if(TARGET test_monitor_socket_contract)
  set_tests_properties(test_monitor_socket_contract PROPERTIES TIMEOUT 60)
endif()
if(TARGET test_monitor_perf_contract)
  set_tests_properties(test_monitor_perf_contract PROPERTIES TIMEOUT 30)
endif()
if(TARGET test_discovery_socket_auto_connect_policy)
  set_tests_properties(
    test_discovery_socket_auto_connect_policy
    PROPERTIES
      TIMEOUT 60
      RESOURCE_LOCK "${NETWORK_INTEGRATION_RESOURCE_LOCK}")
endif()
if(TARGET test_helper_request_sequence_failure)
  set_tests_properties(
    test_helper_request_sequence_failure
    PROPERTIES
      TIMEOUT 30)
endif()
if(TARGET test_multi_pubsub_benchmark_process)
  if(TARGET comp_src_pubsub_server)
    add_dependencies(
      test_multi_pubsub_benchmark_process
      comp_src_pubsub_server)
  endif()
  if(TARGET comp_src_pubsub_client)
    add_dependencies(
      test_multi_pubsub_benchmark_process
      comp_src_pubsub_client)
  endif()
  set_tests_properties(
    test_multi_pubsub_benchmark_process
    PROPERTIES
      TIMEOUT 60
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
      RESOURCE_LOCK "${NETWORK_INTEGRATION_RESOURCE_LOCK}")
endif()
if(TARGET test_multi_dealer_dealer_benchmark_process)
  if(TARGET comp_src_dealer_dealer_server)
    add_dependencies(
      test_multi_dealer_dealer_benchmark_process
      comp_src_dealer_dealer_server)
  endif()
  if(TARGET comp_src_dealer_dealer_client)
    add_dependencies(
      test_multi_dealer_dealer_benchmark_process
      comp_src_dealer_dealer_client)
  endif()
  set_tests_properties(
    test_multi_dealer_dealer_benchmark_process
    PROPERTIES
      TIMEOUT 180
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
      RESOURCE_LOCK "${NETWORK_INTEGRATION_RESOURCE_LOCK}")
endif()
if(TARGET test_multi_spot_benchmark_process)
  if(TARGET comp_src_spot_server)
    add_dependencies(
      test_multi_spot_benchmark_process
      comp_src_spot_server)
  endif()
  if(TARGET comp_src_spot_client)
    add_dependencies(
      test_multi_spot_benchmark_process
      comp_src_spot_client)
  endif()
  if(TARGET comp_src_spot_reqrep_server)
    add_dependencies(
      test_multi_spot_benchmark_process
      comp_src_spot_reqrep_server)
  endif()
  if(TARGET comp_src_spot_reqrep_client)
    add_dependencies(
      test_multi_spot_benchmark_process
      comp_src_spot_reqrep_client)
  endif()
  if(TARGET comp_src_spot_sendsend_server)
    add_dependencies(
      test_multi_spot_benchmark_process
      comp_src_spot_sendsend_server)
  endif()
  if(TARGET comp_src_spot_sendsend_client)
    add_dependencies(
      test_multi_spot_benchmark_process
      comp_src_spot_sendsend_client)
  endif()
  set_tests_properties(
    test_multi_spot_benchmark_process
    PROPERTIES
      TIMEOUT 360
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
      RESOURCE_LOCK "${NETWORK_INTEGRATION_RESOURCE_LOCK}")
endif()
if(TARGET test_single_pair_benchmark_process)
  if(TARGET perf_pair)
    add_dependencies(
      test_single_pair_benchmark_process
      perf_pair)
  endif()
  set_tests_properties(
    test_single_pair_benchmark_process
    PROPERTIES
      TIMEOUT 30
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
      RESOURCE_LOCK "${NETWORK_INTEGRATION_RESOURCE_LOCK}")
endif()
if(TARGET test_single_spot_benchmark_process)
  if(TARGET perf_spot)
    add_dependencies(
      test_single_spot_benchmark_process
      perf_spot)
  endif()
  set_tests_properties(
    test_single_spot_benchmark_process
    PROPERTIES
      TIMEOUT 120
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
      RESOURCE_LOCK "${NETWORK_INTEGRATION_RESOURCE_LOCK}")
endif()
if(TARGET test_stream_socket)
  set_tests_properties(test_stream_socket PROPERTIES TIMEOUT 60)
endif()
if(TARGET test_stream_threadsafe)
  set_tests_properties(test_stream_threadsafe PROPERTIES TIMEOUT 30)
endif()
if(TARGET test_stream_send_blocking_wakeup)
  set_tests_properties(test_stream_send_blocking_wakeup PROPERTIES TIMEOUT 30)
endif()
if(TARGET test_stream_fastpath)
  set_tests_properties(test_stream_fastpath PROPERTIES TIMEOUT 30)
endif()
function(add_env_case_test test_name target_name test_case)
  add_test(
    NAME ${test_name}
    COMMAND ${CMAKE_COMMAND} -E env
            ZLINK_TEST_CASE=${test_case}
            $<TARGET_FILE:${target_name}>)
endfunction()

function(extract_named_unity_cases out_var source_file macro_name)
  file(
    STRINGS "${source_file}" case_lines
    REGEX "^[ \t]*${macro_name} \\([A-Za-z0-9_]+\\);")
  set(cases)
  foreach(line IN LISTS case_lines)
    string(
      REGEX REPLACE
      "^[ \t]*${macro_name} \\(([A-Za-z0-9_]+)\\);.*$"
      "\\1"
      case_name
      "${line}")
    list(APPEND cases "${case_name}")
  endforeach()
  set(${out_var} "${cases}" PARENT_SCOPE)
endfunction()

function(assert_named_case_coverage label source_file macro_name expected_cases_var)
  extract_named_unity_cases(actual_cases "${source_file}" "${macro_name}")
  set(expected_cases "${${expected_cases_var}}")
  if(NOT "${actual_cases}" STREQUAL "${expected_cases}")
    message(
      FATAL_ERROR
      "${label} split case coverage mismatch.\n"
      "Source file: ${source_file}\n"
      "Expected cases: ${expected_cases}\n"
      "Actual cases: ${actual_cases}")
  endif()
endfunction()

function(add_env_case_tests_from_specs target_name specs_var)
  foreach(spec IN LISTS ${specs_var})
    string(REPLACE "=" ";" parts "${spec}")
    list(LENGTH parts part_count)
    if(NOT part_count EQUAL 2)
      message(FATAL_ERROR "Invalid case spec '${spec}'")
    endif()
    list(GET parts 0 test_name)
    list(GET parts 1 test_case)
    add_env_case_test(${test_name} ${target_name} ${test_case})
  endforeach()
endfunction()

function(extract_test_names_from_specs out_var specs_var)
  set(test_names)
  foreach(spec IN LISTS ${specs_var})
    string(REPLACE "=" ";" parts "${spec}")
    list(GET parts 0 test_name)
    list(APPEND test_names "${test_name}")
  endforeach()
  set(${out_var} "${test_names}" PARENT_SCOPE)
endfunction()

set(NETWORK_INTEGRATION_RESOURCE_LOCK "zlink-network-integration")
set(SPOT_TEST_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
set(SPOT_SPLIT_TEST_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

set(SPOT_SERVICE_INTROSPECTION_CASES
    test_spot_pub_sub_options_and_routing_ids
    test_spot_callback_model_receive_regression
    test_spot_recv_model_receive_regression
    test_spot_node_default_handle_owner_keeps_defaults_private
    test_spot_unified_spot_basic
    test_spot_node_snapshot_status_peers_subjects
    test_discovery_local_value_metadata_contract)

set(SPOT_SERVICE_INTROSPECTION_TEST_SPECS
    test_spot_service_introspection_pub_sub_options=test_spot_pub_sub_options_and_routing_ids
    test_spot_service_introspection_callback_recv_regression=test_spot_callback_model_receive_regression
    test_spot_service_introspection_recv_model_regression=test_spot_recv_model_receive_regression
    test_spot_service_introspection_default_handle_owner=test_spot_node_default_handle_owner_keeps_defaults_private
    test_spot_service_introspection_unified_spot=test_spot_unified_spot_basic
    test_spot_service_introspection_snapshots=test_spot_node_snapshot_status_peers_subjects
    test_spot_service_introspection_metadata_local=test_discovery_local_value_metadata_contract)

set(SPOT_PUBSUB_CASES
    test_spot_peer_tcp
    test_spot_peer_tcp_reverse_publish
    test_spot_multi_publisher
    test_spot_node_direct_local_and_child_interop
    test_spot_sub_handler_basic
    test_spot_recv_callback_isolated_by_handle
    test_spot_node_discovery_direct_and_child_interop)

set(SPOT_PUBSUB_PEER_TEST_SPECS
    test_spot_pubsub_scenario_peer_tcp=test_spot_peer_tcp
    test_spot_pubsub_scenario_peer_tcp_reverse_publish=test_spot_peer_tcp_reverse_publish)

set(SPOT_PUBSUB_LOCAL_TEST_SPECS
    test_spot_pubsub_scenario_multi_publisher=test_spot_multi_publisher
    test_spot_pubsub_scenario_node_child_interop=test_spot_node_direct_local_and_child_interop
    test_spot_pubsub_scenario_sub_handler_basic=test_spot_sub_handler_basic
    test_spot_pubsub_scenario_recv_handle_isolation=test_spot_recv_callback_isolated_by_handle
    test_spot_pubsub_scenario_node_discovery_interop=test_spot_node_discovery_direct_and_child_interop)

set(STREAM_SOCKET_REGRESSION_TEST_SPECS
    test_stream_socket_callback_zero_byte=test_stream_callback_echo_single_zero_byte
    test_stream_socket_raw_callback_ready_contract=test_stream_raw_callback_ready_precedes_first_payload_contract
    test_stream_socket_raw_multiclient_ready_regression=test_stream_raw_multiclient_strict_ready_gating_regression
    test_stream_socket_recv_multiclient_ready_regression=test_stream_recv_multiclient_strict_ready_gating_regression
    test_stream_socket_raw_multiclient_load=test_stream_raw_multiclient_load_integrity
    test_stream_socket_raw_multiclient_load_send_ready=test_stream_raw_multiclient_load_integrity_with_send_ready_handler)

set(STREAM_THREADSAFE_REGRESSION_TEST_SPECS
    test_stream_threadsafe_callback_handoff=test_stream_callback_handoff_to_worker_thread_send_msg_is_safe
    test_stream_threadsafe_callback_queue_load=test_stream_callback_queue_handoff_with_send_ready_under_load_is_safe
    test_stream_threadsafe_recv_queue_load=test_stream_recv_handler_queue_handoff_with_send_ready_under_load_is_safe
    test_stream_threadsafe_recv_queue_timed_window=test_stream_recv_handler_queue_handoff_with_send_ready_timed_window_drains_cleanly
    test_stream_threadsafe_send_msg=test_stream_send_msg_is_thread_safe
    test_stream_threadsafe_runtime_reads=test_stream_runtime_reads_are_safe_during_send
    test_stream_threadsafe_socket_runtime_reads=test_socket_runtime_reads_are_safe_during_connect_disconnect
    test_stream_threadsafe_client_churn=test_stream_rapid_client_churn_during_send)

set(SERVICE_INTROSPECTION_CASES
    test_discovery_monitor_and_routing_id
    test_discovery_ready_changed_contract
    test_registry_topology_snapshot_and_remote_query
    test_registry_raw_socket_topology_and_summary_query
    test_monitor_closed_event_on_service_destroy
    test_discovery_monitor_reports_service_up_then_down_in_order
    test_discovery_monitor_callback_parent_destroy_returns_ebusy
    test_registry_control_path_queries_are_safe_during_concurrent_updates
    test_discovery_control_path_reads_are_safe_during_concurrent_updates
    test_discovery_registry_transport_restriction
    test_discovery_registry_transport_allowed_ws
    test_discovery_registry_transport_allowed_tls
    test_registry_peer_transport_restriction_and_tcp_sync
    test_registry_peer_transport_mixed_ws_sync)

set(SERVICE_INTROSPECTION_TEST_SPECS
    test_service_introspection_discovery_monitor=test_discovery_monitor_and_routing_id
    test_service_introspection_discovery_ready_changed=test_discovery_ready_changed_contract
    test_service_introspection_topology_snapshot=test_registry_topology_snapshot_and_remote_query
    test_service_introspection_raw_topology=test_registry_raw_socket_topology_and_summary_query
    test_service_introspection_closed_event=test_monitor_closed_event_on_service_destroy
    test_service_introspection_discovery_ordering=test_discovery_monitor_reports_service_up_then_down_in_order
    test_service_introspection_discovery_self_close=test_discovery_monitor_callback_parent_destroy_returns_ebusy
    test_service_introspection_registry_control_path=test_registry_control_path_queries_are_safe_during_concurrent_updates
    test_service_introspection_discovery_control_path=test_discovery_control_path_reads_are_safe_during_concurrent_updates
    test_service_introspection_transport_restriction=test_discovery_registry_transport_restriction
    test_service_introspection_transport_ws=test_discovery_registry_transport_allowed_ws
    test_service_introspection_transport_tls=test_discovery_registry_transport_allowed_tls
    test_service_introspection_registry_peer_sync=test_registry_peer_transport_restriction_and_tcp_sync
    test_service_introspection_registry_peer_mixed_ws=test_registry_peer_transport_mixed_ws_sync)

assert_named_case_coverage(
  "test_spot_pubsub_scenario"
  "${CMAKE_CURRENT_LIST_DIR}/e2e/spot/test_spot_pubsub_scenario.cpp"
  "RUN_SPOT_TEST"
  SPOT_PUBSUB_CASES)
assert_named_case_coverage(
  "test_spot_service_introspection"
  "${CMAKE_CURRENT_LIST_DIR}/e2e/spot/test_spot_service_introspection.cpp"
  "RUN_SPOT_INTROSPECTION_TEST"
  SPOT_SERVICE_INTROSPECTION_CASES)
if(TARGET test_spot_service_introspection)
  set_tests_properties(
    test_spot_service_introspection
    PROPERTIES
      TIMEOUT 120
      SKIP_RETURN_CODE 77
      WORKING_DIRECTORY "${SPOT_TEST_WORKING_DIRECTORY}"
      RESOURCE_LOCK "${NETWORK_INTEGRATION_RESOURCE_LOCK}")

  add_env_case_tests_from_specs(
    test_spot_service_introspection
    SPOT_SERVICE_INTROSPECTION_TEST_SPECS)
  extract_test_names_from_specs(
    SPOT_SERVICE_INTROSPECTION_TEST_NAMES
    SPOT_SERVICE_INTROSPECTION_TEST_SPECS)

  set_tests_properties(
    ${SPOT_SERVICE_INTROSPECTION_TEST_NAMES}
    PROPERTIES
      TIMEOUT 120
      SKIP_RETURN_CODE 77
      WORKING_DIRECTORY "${SPOT_TEST_WORKING_DIRECTORY}"
      RESOURCE_LOCK "${NETWORK_INTEGRATION_RESOURCE_LOCK}")
endif()
if(TARGET test_spot_pubsub_scenario)
  set_tests_properties(
    test_spot_pubsub_scenario
    PROPERTIES
      TIMEOUT 180
      SKIP_RETURN_CODE 77
      WORKING_DIRECTORY "${SPOT_TEST_WORKING_DIRECTORY}"
      RESOURCE_LOCK "${NETWORK_INTEGRATION_RESOURCE_LOCK}"
      ENVIRONMENT
        "ZLINK_TEST_SUITE_MODE=e2e;ZLINK_SPOT_RUN_MULTI_NODE_DISCOVERY=1;ZLINK_SPOT_MULTI_NODE_FIELD_WIDTH=4;ZLINK_SPOT_MULTI_NODE_FIELD_HEIGHT=4;ZLINK_SPOT_MULTI_NODE_COUNT=2")

  add_env_case_tests_from_specs(
    test_spot_pubsub_scenario
    SPOT_PUBSUB_PEER_TEST_SPECS)
  add_env_case_tests_from_specs(
    test_spot_pubsub_scenario
    SPOT_PUBSUB_LOCAL_TEST_SPECS)
  extract_test_names_from_specs(
    SPOT_PUBSUB_PEER_TEST_NAMES
    SPOT_PUBSUB_PEER_TEST_SPECS)
  extract_test_names_from_specs(
    SPOT_PUBSUB_LOCAL_TEST_NAMES
    SPOT_PUBSUB_LOCAL_TEST_SPECS)

  set_tests_properties(
    ${SPOT_PUBSUB_LOCAL_TEST_NAMES}
    PROPERTIES
      TIMEOUT 60
      SKIP_RETURN_CODE 77
      WORKING_DIRECTORY "${SPOT_SPLIT_TEST_WORKING_DIRECTORY}"
      RESOURCE_LOCK "${NETWORK_INTEGRATION_RESOURCE_LOCK}")

  set_tests_properties(
    ${SPOT_PUBSUB_PEER_TEST_NAMES}
    PROPERTIES
      TIMEOUT 90
      SKIP_RETURN_CODE 77
      WORKING_DIRECTORY "${SPOT_SPLIT_TEST_WORKING_DIRECTORY}"
      RESOURCE_LOCK "${NETWORK_INTEGRATION_RESOURCE_LOCK}")

endif()
if(TARGET test_stream_socket)
  add_env_case_tests_from_specs(
    test_stream_socket
    STREAM_SOCKET_REGRESSION_TEST_SPECS)
  extract_test_names_from_specs(
    STREAM_SOCKET_REGRESSION_TEST_NAMES
    STREAM_SOCKET_REGRESSION_TEST_SPECS)
  set_tests_properties(
    ${STREAM_SOCKET_REGRESSION_TEST_NAMES}
    PROPERTIES
      TIMEOUT 60
      RESOURCE_LOCK "${NETWORK_INTEGRATION_RESOURCE_LOCK}")
endif()
if(TARGET test_stream_threadsafe)
  add_env_case_tests_from_specs(
    test_stream_threadsafe
    STREAM_THREADSAFE_REGRESSION_TEST_SPECS)
  extract_test_names_from_specs(
    STREAM_THREADSAFE_REGRESSION_TEST_NAMES
    STREAM_THREADSAFE_REGRESSION_TEST_SPECS)
  set_tests_properties(
    ${STREAM_THREADSAFE_REGRESSION_TEST_NAMES}
    PROPERTIES
      TIMEOUT 45
      RESOURCE_LOCK "${NETWORK_INTEGRATION_RESOURCE_LOCK}")
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
if(TARGET test_scenario_stream_zlink_packet)
  set_tests_properties(test_scenario_stream_zlink_packet PROPERTIES TIMEOUT 120)
  set_tests_properties(test_scenario_stream_zlink_packet PROPERTIES LABELS "scenario;stream;zlink_packet")
endif()
if(TARGET test_scenario_stream_asio)
  set_tests_properties(test_scenario_stream_asio PROPERTIES TIMEOUT 120)
  set_tests_properties(test_scenario_stream_asio PROPERTIES LABELS "scenario;stream;asio")
endif()
if(TARGET test_scenario_stream_zmq)
  set_tests_properties(test_scenario_stream_zmq PROPERTIES TIMEOUT 120)
  set_tests_properties(test_scenario_stream_zmq PROPERTIES LABELS "scenario;stream;zmq")
endif()

set(E2E_SERIAL_TESTS
    test_spot_pubsub_scenario
    test_spot_service_introspection)
if(TARGET test_scenario_stream_client)
  list(APPEND E2E_SERIAL_TESTS test_scenario_stream_client)
endif()
if(TARGET test_scenario_stream_zlink)
  list(APPEND E2E_SERIAL_TESTS test_scenario_stream_zlink)
endif()
if(TARGET test_scenario_stream_zlink_packet)
  list(APPEND E2E_SERIAL_TESTS test_scenario_stream_zlink_packet)
endif()
if(TARGET test_scenario_stream_asio)
  list(APPEND E2E_SERIAL_TESTS test_scenario_stream_asio)
endif()
if(TARGET test_scenario_stream_zmq)
  list(APPEND E2E_SERIAL_TESTS test_scenario_stream_zmq)
endif()
if(TARGET test_scenario_router_router_zlink_connect)
  list(APPEND E2E_SERIAL_TESTS test_scenario_router_router_zlink_connect)
endif()

set(REGRESSION_SERIAL_TESTS
    test_router_concurrent_routed_recv
    test_transport_matrix
    ${STREAM_SOCKET_REGRESSION_TEST_NAMES}
    ${STREAM_THREADSAFE_REGRESSION_TEST_NAMES})
if(TARGET test_multi_dealer_dealer_benchmark_process)
  list(APPEND REGRESSION_SERIAL_TESTS test_multi_dealer_dealer_benchmark_process)
endif()
if(TARGET test_multi_spot_benchmark_process)
  list(APPEND REGRESSION_SERIAL_TESTS test_multi_spot_benchmark_process)
endif()

set(INTEGRATION_SERIAL_TESTS ${REGISTERED_TESTS})
list(REMOVE_ITEM INTEGRATION_SERIAL_TESTS ${E2E_SERIAL_TESTS} ${REGRESSION_SERIAL_TESTS})
list(APPEND INTEGRATION_SERIAL_TESTS
     ${SERVICE_INTROSPECTION_TEST_NAMES}
     ${SERVICE_DISCOVERY_TEST_NAMES}
     ${SPOT_SERVICE_INTROSPECTION_TEST_NAMES}
     ${SPOT_PUBSUB_PEER_TEST_NAMES}
     ${SPOT_PUBSUB_LOCAL_TEST_NAMES})
list(REMOVE_ITEM INTEGRATION_SERIAL_TESTS ${E2E_SERIAL_TESTS} ${REGRESSION_SERIAL_TESTS})
list(REMOVE_DUPLICATES INTEGRATION_SERIAL_TESTS)

if(INTEGRATION_SERIAL_TESTS)
  set_tests_properties(${INTEGRATION_SERIAL_TESTS} PROPERTIES RUN_SERIAL TRUE)
  set_property(TEST ${INTEGRATION_SERIAL_TESTS} APPEND PROPERTY LABELS "integration;serial")
endif()

if(E2E_SERIAL_TESTS)
  set_tests_properties(${E2E_SERIAL_TESTS} PROPERTIES RUN_SERIAL TRUE)
  set_property(TEST ${E2E_SERIAL_TESTS} APPEND PROPERTY LABELS "e2e;serial")
endif()

if(REGRESSION_SERIAL_TESTS)
  set_tests_properties(${REGRESSION_SERIAL_TESTS} PROPERTIES
    RUN_SERIAL TRUE
    LABELS "regression;serial")
endif()

# Check whether all tests in the current folder are present
file(READ "${CMAKE_CURRENT_LIST_FILE}" CURRENT_LIST_FILE_CONTENT)
file(GLOB_RECURSE ALL_TEST_SOURCES "test_*.cpp")

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
    test_spot_multi_service_discovery
    test_spot_multi_service_discovery_scale
    test_router_multiple_dealers
    test_reconnect_ivl
    test_reconnect_options
    test_socket_options_fuzzer
    test_spec_router
    test_stream_fastpath
    test_stream_send_blocking_wakeup
    test_stream_socket
    test_sub_forward
    test_system
    test_timeo
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
