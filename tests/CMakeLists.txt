# CMake build script for ZeroMQ tests
cmake_minimum_required(VERSION 3.10)

# On Windows: solution file will be called tests.sln
project(tests)

set(tests
  test_ancillaries
  test_system
  test_pair_inproc
  test_pair_tcp
  test_hwm_pubsub
  test_sub_forward
  test_msg_flags
  test_msg_ffn
  test_connect_resolve
  test_immediate
  test_last_endpoint
  test_router_mandatory
  test_probe_router
  test_disconnect_inproc
  test_ctx_options
  test_ctx_destroy
  test_spec_router
  test_issue_566
  test_shutdown_stress
  test_timeo
  test_many_sockets
  test_diffserv
  test_connect_rid
  test_xpub_nodrop
  test_pub_invert_matching
  test_heartbeats
  test_zmp_metadata
  test_zmp_ws_wss
  test_bind_src_address
  test_capabilities
  test_router_handover
  test_xpub_manual
  test_xpub_topic
  test_xpub_welcome_msg
  test_xpub_verbose
  test_bind_after_connect_tcp
  test_socket_null
  test_reconnect_ivl
  test_reconnect_options
  test_pubsub_filter_xpub
  test_router_multiple_dealers
  test_stream_socket
  test_transport_matrix
  routing-id/test_router_auto_id_format
  routing-id/test_stream_routing_id_size
  routing-id/test_connect_rid_string_alias
  monitoring/test_monitor_enhanced)

if(NOT WIN32)
  list(APPEND tests test_connect_null_fuzzer test_bind_null_fuzzer test_connect_fuzzer test_bind_fuzzer)
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

if(ZMQ_HAVE_IPC)
  list(APPEND tests test_ipc_wildcard test_pair_ipc)
endif()

if(NOT WIN32)
  list(
    APPEND
    tests
    test_proxy
    test_getsockopt_memset
    test_router_mandatory_hwm)

  if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    list(APPEND tests test_abstract_ipc)
  endif()
endif()

# ASIO poller test - verifies the ASIO-based I/O poller
list(APPEND tests test_asio_poller)

# ASIO connect test - tests async_accept/async_connect
list(APPEND tests test_asio_connect)

# ASIO TCP engine test - tests true proactor mode
list(APPEND tests test_asio_tcp)

# ASIO SSL test - tests SSL/TLS support
# This test compiles with or without SSL, skipping tests if SSL is not enabled
list(APPEND tests test_asio_ssl)

# ASIO WebSocket test - tests WebSocket support
# This test compiles with or without WebSocket, skipping tests if WS is not enabled
list(APPEND tests test_asio_ws)

# add location of platform.hpp for Windows builds
if(WIN32)
  add_definitions(-DZMQ_CUSTOM_PLATFORM_HPP)
  add_definitions(-D_WINSOCK_DEPRECATED_NO_WARNINGS)

  # Same name on 64bit systems
  link_libraries(ws2_32.lib)
endif()

add_library(
  unity STATIC
  "${CMAKE_CURRENT_LIST_DIR}/../external/unity/unity.c" "${CMAKE_CURRENT_LIST_DIR}/../external/unity/unity.h"
  "${CMAKE_CURRENT_LIST_DIR}/../external/unity/unity_internals.h")
set_target_properties(unity PROPERTIES PUBLIC_HEADER "${CMAKE_CURRENT_LIST_DIR}/../external/unity/unity.h")
target_compile_definitions(unity PUBLIC "UNITY_USE_COMMAND_LINE_ARGS" "UNITY_EXCLUDE_FLOAT")
target_include_directories(unity PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../external/unity")

set(TESTUTIL_SOURCES
  testutil.cpp
  testutil.hpp
  testutil_monitoring.cpp
  testutil_monitoring.hpp
  testutil_unity.cpp
  testutil_unity.hpp)

if(BUILD_STATIC)
  add_library(testutil-static STATIC ${TESTUTIL_SOURCES})
  target_link_libraries(testutil-static libzmq-static ${OPTIONAL_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} unity)
endif()

if(BUILD_SHARED)
  add_library(testutil STATIC ${TESTUTIL_SOURCES})
  target_link_libraries(testutil libzmq ${OPTIONAL_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} unity)
endif()

if(BUILD_STATIC AND NOT BUILD_SHARED)
  # use testutil-static for both tests and unit tests
  set(TESTUTIL_LIB testutil-static)
else()
  # use testutil for tests and testutil-static for unit tests
  set(TESTUTIL_LIB testutil)
endif()

if(MSVC_VERSION LESS 1700)
  set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/../external/unity/unity.c" PROPERTIES LANGUAGE CXX)
endif()

if(MSVC_VERSION LESS 1600)
  target_compile_definitions(unity PUBLIC "UNITY_EXCLUDE_STDINT_H")
endif()

# add include dirs for all targets
# Note: Using CMAKE_SOURCE_DIR instead of ZeroMQ_SOURCE_DIR since project name is now zlink
include_directories("${CMAKE_SOURCE_DIR}/include" "${CMAKE_BINARY_DIR}")
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
    find_path(LIBZMQ_PATH "libzmq.lib" PATHS "../bin/Win32/Debug/v120/dynamic")

    if(NOT ${LIBZMQ_PATH} STREQUAL "LIBZMQ_PATH-NOTFOUND")
      set_target_properties(${test} PROPERTIES LINK_FLAGS "/LIBPATH:${LIBZMQ_PATH}")
    endif()
  else()
    # per-test directories not generated on OS X / Darwin
    if(NOT APPLE)
      link_directories(${test} PRIVATE "${ZeroMQ_SOURCE_DIR}/../lib")
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

  # Automatically set library path for tests to find libzmq shared library
  if(WIN32)
    set_tests_properties(${test} PROPERTIES ENVIRONMENT "PATH=$<TARGET_FILE_DIR:libzmq>;$ENV{PATH}")
  elseif(APPLE)
    set_tests_properties(${test} PROPERTIES ENVIRONMENT "DYLD_LIBRARY_PATH=$<TARGET_FILE_DIR:libzmq>:$ENV{DYLD_LIBRARY_PATH}")
  else()
    set_tests_properties(${test} PROPERTIES ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:libzmq>:$ENV{LD_LIBRARY_PATH}")
  endif()

  if(QNX)
    install(TARGETS ${test} RUNTIME DESTINATION bin/)
  endif()
endforeach()

# override timeout for these tests
set_tests_properties(test_heartbeats PROPERTIES TIMEOUT 60)
set_tests_properties(test_reconnect_ivl PROPERTIES TIMEOUT 15)
set_tests_properties(test_issue_566 PROPERTIES TIMEOUT 30)
set_tests_properties(test_transport_matrix PROPERTIES TIMEOUT 30)

# Check whether all tests in the current folder are present
file(READ "${CMAKE_CURRENT_LIST_FILE}" CURRENT_LIST_FILE_CONTENT)
file(GLOB ALL_TEST_SOURCES "test_*.cpp" "*/test_*.cpp")

set(UNREGISTERED_TEST_ALLOWLIST
    test_bind_ws_fuzzer
    test_connect_fuzzer
    test_connect_null_fuzzer
    test_socket_options_fuzzer
    test_use_fd
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
