cmake_minimum_required(VERSION 3.20)
project(zlink_framework_cpp LANGUAGES C CXX)

include(GNUInstallDirs)
include(FetchContent)
include(CMakePackageConfigHelpers)
find_package(Threads REQUIRED)

option(ZLINK_FRAMEWORK_CPP_BUILD_TESTS "Build C++ framework tests" ON)
option(ZLINK_FRAMEWORK_CPP_BUILD_SAMPLES "Build C++ framework samples" ON)
option(ZLINK_STREAM_CONNECTOR_WITH_JSON "Enable Stream Connector JSON helpers" ON)
option(ZLINK_STREAM_CONNECTOR_WITH_MESSAGEPACK "Enable Stream Connector MessagePack helpers" OFF)
option(ZLINK_STREAM_CONNECTOR_WITH_PROTOBUF "Enable Stream Connector Protobuf helpers" OFF)
option(ZLINK_STREAM_CONNECTOR_WITH_LZ4 "Enable Stream Connector LZ4 compression" ON)

set(ZLINK_FRAMEWORK_CPP_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(ZLINK_FRAMEWORK_CPP_REPO_ROOT "${ZLINK_FRAMEWORK_CPP_DIR}/../../.." CACHE PATH
  "Path to the zlink repository root")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    set(ZLINK_FRAMEWORK_CPP_NATIVE_RUNTIME
      "${ZLINK_FRAMEWORK_CPP_REPO_ROOT}/bindings/cpp/native/linux-aarch64/libzlink.so")
  else()
    set(ZLINK_FRAMEWORK_CPP_NATIVE_RUNTIME
      "${ZLINK_FRAMEWORK_CPP_REPO_ROOT}/bindings/cpp/native/linux-x86_64/libzlink.so")
  endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    set(ZLINK_FRAMEWORK_CPP_NATIVE_RUNTIME
      "${ZLINK_FRAMEWORK_CPP_REPO_ROOT}/bindings/cpp/native/darwin-aarch64/libzlink.dylib")
  else()
    set(ZLINK_FRAMEWORK_CPP_NATIVE_RUNTIME
      "${ZLINK_FRAMEWORK_CPP_REPO_ROOT}/bindings/cpp/native/darwin-x86_64/libzlink.dylib")
  endif()
elseif(WIN32)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|aarch64|arm64)$")
    set(ZLINK_FRAMEWORK_CPP_NATIVE_RUNTIME
      "${ZLINK_FRAMEWORK_CPP_REPO_ROOT}/bindings/cpp/native/windows-aarch64/zlink.dll")
  else()
    set(ZLINK_FRAMEWORK_CPP_NATIVE_RUNTIME
      "${ZLINK_FRAMEWORK_CPP_REPO_ROOT}/bindings/cpp/native/windows-x86_64/zlink.dll")
  endif()
endif()

if(NOT TARGET zlink::cpp)
  set(ZLINK_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(ZLINK_CPP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
  add_subdirectory(
    "${ZLINK_FRAMEWORK_CPP_REPO_ROOT}/bindings/cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/bindings_cpp")
endif()

add_library(zlink_framework STATIC
  framework/src/runtime/channels/channel_packet_dispatcher.cpp
  framework/src/runtime/channels/channel_bundle_factory.cpp
  framework/src/runtime/channels/channel_pending_requests.cpp
  framework/src/runtime/channels/channel_message_pump.cpp
  framework/src/runtime/channels/channel_receive_loop.cpp
  framework/src/runtime/channels/channel_reply_writer.cpp
  framework/src/runtime/channels/channel_runtime_bundle.cpp
  framework/src/runtime/channels/channel_runtime_manager.cpp
  framework/src/runtime/channels/channel_runtime.cpp
  framework/src/runtime/channels/route_channel_registration.cpp
  framework/src/runtime/channels/route_channel_runtime.cpp
  framework/src/runtime/channels/route_connection_set.cpp
  framework/src/runtime/channels/route_handler_invoker.cpp
  framework/src/runtime/channels/route_handler_registry.cpp
  framework/src/runtime/channels/route_internal_packet_dispatcher.cpp
  framework/src/runtime/channels/route_packet_dispatcher.cpp
  framework/src/runtime/channels/route_receive_pump.cpp
  framework/src/runtime/actors/actor_gateway_runtime.cpp
  framework/src/runtime/backend/native_route_backend.cpp
  framework/src/runtime/host/assembly.cpp
  framework/src/runtime/configuration/configuration.cpp
  framework/src/runtime/configuration/services.cpp
  framework/src/runtime/codecs/serializer.cpp
  framework/src/runtime/diagnostics/logging.cpp
  framework/src/runtime/diagnostics/monitoring_runtime.cpp
  framework/src/runtime/dispatch/offload_executor.cpp
  framework/src/runtime/execution/serial_execution_queue.cpp
  framework/src/runtime/handlers/handler_registry.cpp
  framework/src/runtime/dispatch/projection.cpp
  framework/src/runtime/host/app.cpp
  framework/src/runtime/host/framework_runtime.cpp
  framework/src/runtime/messaging/client_call_codec.cpp
  framework/src/runtime/messaging/envelope_codec.cpp
  framework/src/runtime/messaging/pending_operation.cpp
  framework/src/runtime/messaging/pending_submit.cpp
  framework/src/runtime/messaging/request_failure_mapper.cpp
  framework/src/runtime/messaging/submit_queue.cpp
  framework/src/runtime/registry/registry_runtime.cpp
  framework/src/runtime/spots/spot_runtime.cpp
  framework/src/runtime/streams/stream_runtime.cpp
  framework/src/runtime/timers/timer_runtime.cpp
  framework/src/runtime/transport/endpoint.cpp)
add_library(zlink::framework ALIAS zlink_framework)
set_target_properties(zlink_framework PROPERTIES EXPORT_NAME framework)
target_compile_features(zlink_framework PUBLIC cxx_std_20)
target_include_directories(zlink_framework PUBLIC
  $<BUILD_INTERFACE:${ZLINK_FRAMEWORK_CPP_DIR}/framework/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_include_directories(zlink_framework PRIVATE
  ${ZLINK_FRAMEWORK_CPP_DIR}/framework/src)
target_link_libraries(zlink_framework PUBLIC zlink::cpp)
find_package(nlohmann_json REQUIRED)
target_include_directories(zlink_framework PUBLIC
  $<BUILD_INTERFACE:${ZLINK_FRAMEWORK_CPP_REPO_ROOT}/bindings/cpp/codecs/zlink-codec-json/include>)
target_link_libraries(zlink_framework PUBLIC nlohmann_json::nlohmann_json)

add_library(zlink_stream_connector STATIC
  connector/src/runtime/calls/zlink_stream_calls.cpp
  connector/src/runtime/connector_callbacks.cpp
  connector/src/runtime/connector_lifecycle.cpp
  connector/src/runtime/connector_runtime.cpp
  connector/src/runtime/heartbeat_monitor.cpp
  connector/src/runtime/protocol/compression/lz4_compression_codec.cpp
  connector/src/runtime/protocol/framing/frame_codec.cpp
  connector/src/runtime/protocol/framing.cpp
  connector/src/runtime/protocol/header_codec.cpp
  connector/src/runtime/protocol/metadata_codec.cpp
  connector/src/runtime/protocol/packet_name_resolver.cpp
  connector/src/runtime/receive_dispatcher.cpp
  connector/src/runtime/transport/stream_connection.cpp
  connector/src/runtime/transport/stream_transport_factory.cpp
  connector/src/runtime/transport/websocket_connection.cpp)
add_library(zlink::stream_connector ALIAS zlink_stream_connector)
set_target_properties(zlink_stream_connector PROPERTIES
  EXPORT_NAME stream_connector)
target_compile_features(zlink_stream_connector PUBLIC cxx_std_20)
target_include_directories(zlink_stream_connector PUBLIC
  $<BUILD_INTERFACE:${ZLINK_FRAMEWORK_CPP_DIR}/connector/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_include_directories(zlink_stream_connector PRIVATE
  ${ZLINK_FRAMEWORK_CPP_DIR}/connector/src)
target_link_libraries(zlink_stream_connector PUBLIC zlink::cpp)
target_link_libraries(zlink_stream_connector PRIVATE Threads::Threads)
if(ZLINK_STREAM_CONNECTOR_WITH_LZ4)
  find_path(ZLINK_LZ4_INCLUDE_DIR lz4.h)
  find_library(ZLINK_LZ4_LIBRARY NAMES lz4 liblz4)
  if(ZLINK_LZ4_INCLUDE_DIR AND ZLINK_LZ4_LIBRARY)
    target_include_directories(zlink_stream_connector PRIVATE
      ${ZLINK_LZ4_INCLUDE_DIR})
    target_link_libraries(zlink_stream_connector PRIVATE ${ZLINK_LZ4_LIBRARY})
  else()
    FetchContent_Declare(
      zlink_lz4
      URL https://github.com/lz4/lz4/archive/refs/tags/v1.10.0.tar.gz
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(zlink_lz4)
    target_sources(zlink_stream_connector PRIVATE
      ${zlink_lz4_SOURCE_DIR}/lib/lz4.c)
    target_include_directories(zlink_stream_connector PRIVATE
      ${zlink_lz4_SOURCE_DIR}/lib)
  endif()
endif()
target_compile_definitions(zlink_stream_connector PRIVATE
  $<$<BOOL:${ZLINK_STREAM_CONNECTOR_WITH_MESSAGEPACK}>:ZLINK_STREAM_CONNECTOR_WITH_MESSAGEPACK>
  $<$<BOOL:${ZLINK_STREAM_CONNECTOR_WITH_PROTOBUF}>:ZLINK_STREAM_CONNECTOR_WITH_PROTOBUF>
  $<$<BOOL:${ZLINK_STREAM_CONNECTOR_WITH_LZ4}>:ZLINK_STREAM_CONNECTOR_WITH_LZ4>)

add_library(zlink_stream_connector_codecs INTERFACE)
add_library(zlink::stream_connector_codecs ALIAS zlink_stream_connector_codecs)
set_target_properties(zlink_stream_connector_codecs PROPERTIES
  EXPORT_NAME stream_connector_codecs)
target_compile_features(zlink_stream_connector_codecs INTERFACE cxx_std_20)
target_include_directories(zlink_stream_connector_codecs INTERFACE
  $<BUILD_INTERFACE:${ZLINK_FRAMEWORK_CPP_REPO_ROOT}/bindings/cpp/codecs/zlink-codec-json/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(zlink_stream_connector_codecs INTERFACE
  zlink::stream_connector
  nlohmann_json::nlohmann_json)
if(ZLINK_STREAM_CONNECTOR_WITH_MESSAGEPACK AND TARGET zlink::cpp_codec_messagepack)
  target_link_libraries(zlink_stream_connector_codecs INTERFACE
    zlink::cpp_codec_messagepack)
endif()
if(ZLINK_STREAM_CONNECTOR_WITH_PROTOBUF AND TARGET zlink::cpp_codec_protobuf)
  target_link_libraries(zlink_stream_connector_codecs INTERFACE
    zlink::cpp_codec_protobuf)
endif()

add_library(zlink_unreal_stream_connector STATIC
  unreal-connector/Source/ZLinkStreamConnector/Private/ZLinkStreamConnector.cpp)
set_target_properties(zlink_unreal_stream_connector PROPERTIES
  EXPORT_NAME unreal_stream_connector)
target_compile_features(zlink_unreal_stream_connector PUBLIC cxx_std_20)
target_include_directories(zlink_unreal_stream_connector PUBLIC
  ${ZLINK_FRAMEWORK_CPP_DIR}/unreal-connector/Source/ZLinkStreamConnector/Public)
target_include_directories(zlink_unreal_stream_connector PRIVATE
  ${ZLINK_FRAMEWORK_CPP_DIR}/connector/src)
target_link_libraries(zlink_unreal_stream_connector PUBLIC zlink::stream_connector)
if(ZLINK_STREAM_CONNECTOR_WITH_LZ4)
  if(ZLINK_LZ4_INCLUDE_DIR AND ZLINK_LZ4_LIBRARY)
    target_include_directories(zlink_unreal_stream_connector PRIVATE
      ${ZLINK_LZ4_INCLUDE_DIR})
    target_link_libraries(zlink_unreal_stream_connector PRIVATE
      ${ZLINK_LZ4_LIBRARY})
  else()
    target_sources(zlink_unreal_stream_connector PRIVATE
      ${zlink_lz4_SOURCE_DIR}/lib/lz4.c)
    target_include_directories(zlink_unreal_stream_connector PRIVATE
      ${zlink_lz4_SOURCE_DIR}/lib)
  endif()
endif()
target_compile_definitions(zlink_unreal_stream_connector PRIVATE
  $<$<BOOL:${ZLINK_STREAM_CONNECTOR_WITH_LZ4}>:ZLINK_UNREAL_STREAM_CONNECTOR_WITH_LZ4>)

function(add_zlink_framework_extension target_name alias_name)
  add_library(${target_name} INTERFACE)
  add_library(${alias_name} ALIAS ${target_name})
  string(REPLACE "zlink_framework_extension_" "framework_extension_"
    export_name "${target_name}")
  set_target_properties(${target_name} PROPERTIES EXPORT_NAME ${export_name})
  target_compile_features(${target_name} INTERFACE cxx_std_20)
  target_include_directories(${target_name} INTERFACE
    $<BUILD_INTERFACE:${ZLINK_FRAMEWORK_CPP_DIR}/extensions/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  target_link_libraries(${target_name} INTERFACE zlink::framework)
endfunction()

add_zlink_framework_extension(
  zlink_framework_extension_metrics
  zlink::framework_extension_metrics)
add_zlink_framework_extension(
  zlink_framework_extension_tracing
  zlink::framework_extension_tracing)
add_zlink_framework_extension(
  zlink_framework_extension_kafka_bridge
  zlink::framework_extension_kafka_bridge)
add_zlink_framework_extension(
  zlink_framework_extension_grpc_bridge
  zlink::framework_extension_grpc_bridge)
add_zlink_framework_extension(
  zlink_framework_extension_http_gateway
  zlink::framework_extension_http_gateway)
add_zlink_framework_extension(
  zlink_framework_extension_advanced_retry
  zlink::framework_extension_advanced_retry)
add_zlink_framework_extension(
  zlink_framework_extension_dead_letter_storage
  zlink::framework_extension_dead_letter_storage)
add_zlink_framework_extension(
  zlink_framework_extension_flatbuffers
  zlink::framework_extension_flatbuffers)
add_zlink_framework_extension(
  zlink_framework_extension_yaml_config
  zlink::framework_extension_yaml_config)
add_zlink_framework_extension(
  zlink_framework_extension_custom_codec
  zlink::framework_extension_custom_codec)
add_zlink_framework_extension(
  zlink_framework_extension_custom_transport
  zlink::framework_extension_custom_transport)

install(TARGETS
  zlink_framework
  zlink_framework_extension_metrics
  zlink_framework_extension_tracing
  zlink_framework_extension_kafka_bridge
  zlink_framework_extension_grpc_bridge
  zlink_framework_extension_http_gateway
  zlink_framework_extension_advanced_retry
  zlink_framework_extension_dead_letter_storage
  zlink_framework_extension_flatbuffers
  zlink_framework_extension_yaml_config
  zlink_framework_extension_custom_codec
  zlink_framework_extension_custom_transport
  EXPORT zlink_framework_cppTargets)
install(TARGETS
  zlink_stream_connector
  zlink_stream_connector_codecs
  EXPORT zlink_stream_connector_cppTargets)
install(DIRECTORY framework/include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.hpp")
install(DIRECTORY
  ${ZLINK_FRAMEWORK_CPP_REPO_ROOT}/bindings/cpp/codecs/zlink-codec-json/include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.hpp")
install(DIRECTORY extensions/include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.hpp")
install(DIRECTORY connector/include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.hpp")
if(ZLINK_FRAMEWORK_CPP_NATIVE_RUNTIME AND EXISTS "${ZLINK_FRAMEWORK_CPP_NATIVE_RUNTIME}")
  if(WIN32)
    install(FILES "${ZLINK_FRAMEWORK_CPP_NATIVE_RUNTIME}"
      DESTINATION ${CMAKE_INSTALL_BINDIR})
  else()
    install(FILES "${ZLINK_FRAMEWORK_CPP_NATIVE_RUNTIME}"
      DESTINATION ${CMAKE_INSTALL_LIBDIR})
  endif()
endif()
install(EXPORT zlink_framework_cppTargets
  NAMESPACE zlink::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zlink_framework_cpp)
install(EXPORT zlink_stream_connector_cppTargets
  NAMESPACE zlink::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zlink_stream_connector_cpp)

configure_package_config_file(
  "${CMAKE_CURRENT_LIST_DIR}/cmake/zlink_framework_cppConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/zlink_framework_cppConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zlink_framework_cpp)
configure_package_config_file(
  "${CMAKE_CURRENT_LIST_DIR}/cmake/zlink_stream_connector_cppConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/zlink_stream_connector_cppConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zlink_stream_connector_cpp)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/zlink_framework_cppConfig.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zlink_framework_cpp)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/zlink_stream_connector_cppConfig.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zlink_stream_connector_cpp)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  enable_testing()
endif()

if(ZLINK_FRAMEWORK_CPP_BUILD_TESTS)
  find_package(GTest QUIET)
  if(NOT GTest_FOUND)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
      googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(googletest)
  endif()
  if(TARGET gtest AND NOT TARGET GTest::gtest)
    add_library(GTest::gtest ALIAS gtest)
  endif()
  if(TARGET gmock AND NOT TARGET GTest::gmock)
    add_library(GTest::gmock ALIAS gmock)
  endif()
  if(TARGET gtest_main AND NOT TARGET GTest::gtest_main)
    add_library(GTest::gtest_main ALIAS gtest_main)
  endif()

  function(target_link_zlink_test_harness target_name)
    target_link_libraries(${target_name} PRIVATE GTest::gtest GTest::gmock)
  endfunction()

  function(add_zlink_framework_test target_name source_file labels)
    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PRIVATE zlink::framework)
    target_link_zlink_test_harness(${target_name})
    target_include_directories(${target_name} PRIVATE
      ${ZLINK_FRAMEWORK_CPP_DIR}/framework/src)
    add_test(NAME ${target_name} COMMAND ${target_name})
    set_tests_properties(${target_name} PROPERTIES
      LABELS "${labels}"
      TIMEOUT 30)
  endfunction()

  function(add_zlink_connector_test target_name source_file labels)
    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PRIVATE zlink::stream_connector)
    target_link_zlink_test_harness(${target_name})
    target_include_directories(${target_name} PRIVATE
      ${ZLINK_FRAMEWORK_CPP_DIR}/connector/src)
    add_test(NAME ${target_name} COMMAND ${target_name})
    set_tests_properties(${target_name} PROPERTIES
      LABELS "${labels}"
      TIMEOUT 30)
  endfunction()

  add_executable(test_cpp_framework_contract_headers
    tests/contract/test_cpp_framework_contract_headers.cpp)
  target_link_libraries(test_cpp_framework_contract_headers PRIVATE
    zlink::framework
    zlink::stream_connector
    GTest::gtest
    GTest::gmock)
  add_test(NAME test_cpp_framework_contract_headers
    COMMAND test_cpp_framework_contract_headers)
  set_tests_properties(test_cpp_framework_contract_headers PROPERTIES
    LABELS "framework-contract"
    TIMEOUT 30)

  add_executable(test_cpp_framework_sample_parity
    tests/contract/test_cpp_framework_sample_parity.cpp)
  target_link_libraries(test_cpp_framework_sample_parity PRIVATE
    zlink::framework
    zlink::stream_connector
    GTest::gtest
    GTest::gmock)
  add_test(NAME test_cpp_framework_sample_parity
    COMMAND test_cpp_framework_sample_parity)
  set_tests_properties(test_cpp_framework_sample_parity PROPERTIES
    LABELS "framework-contract;framework-sample-parity"
    TIMEOUT 30)

  add_executable(test_cpp_framework_layout_contract
    tests/contract/test_cpp_framework_layout_contract.cpp)
  target_compile_definitions(test_cpp_framework_layout_contract PRIVATE
    ZLINK_FRAMEWORK_CPP_SOURCE_DIR="${ZLINK_FRAMEWORK_CPP_DIR}")
  target_compile_features(test_cpp_framework_layout_contract PRIVATE cxx_std_20)
  target_link_zlink_test_harness(test_cpp_framework_layout_contract)
  add_test(NAME test_cpp_framework_layout_contract
    COMMAND test_cpp_framework_layout_contract)
  set_tests_properties(test_cpp_framework_layout_contract PROPERTIES
    LABELS "framework-contract"
    TIMEOUT 30)

  add_zlink_framework_test(
    test_cpp_framework_app_host
    tests/unit/test_cpp_framework_app_host.cpp
    "framework-unit")
  add_zlink_framework_test(
    test_cpp_framework_DI_scope
    tests/unit/test_cpp_framework_di.cpp
    "framework-unit;framework-regression;DI;scope")
  add_zlink_framework_test(
    test_cpp_framework_runtime_integration
    tests/unit/test_cpp_framework_runtime_integration.cpp
    "framework-unit;framework-integration;runtime")
  add_zlink_framework_test(
    test_cpp_framework_serializer_registry
    tests/unit/test_cpp_framework_serializer_registry.cpp
    "framework-unit;serializer")
  add_zlink_framework_test(
    test_cpp_framework_handler_registry
    tests/unit/test_cpp_framework_handler_registry.cpp
    "framework-unit;handler")
  add_zlink_framework_test(
    test_cpp_framework_channel_messaging
    tests/unit/test_cpp_framework_channel_messaging.cpp
    "framework-integration;framework-regression;channel")
  add_zlink_framework_test(
    test_cpp_framework_backpressure_reliability
    tests/unit/test_cpp_framework_backpressure_reliability.cpp
    "framework-unit;framework-regression;backpressure;reliability")
  add_zlink_framework_test(
    test_cpp_framework_spot_runtime
    tests/unit/test_cpp_framework_spot_runtime.cpp
    "framework-integration;framework-regression;spot")
  add_zlink_framework_test(
    test_cpp_framework_spot_timer
    tests/unit/test_cpp_framework_spot_timer.cpp
    "framework-unit;framework-integration;framework-regression;timer")
  add_zlink_framework_test(
    test_cpp_framework_stream_framework
    tests/unit/test_cpp_framework_stream_framework.cpp
    "framework-integration;framework-regression;stream")
  add_zlink_framework_test(
    test_cpp_framework_ActorGateway_actor_session_relay
    tests/unit/test_cpp_framework_actor_gateway.cpp
    "framework-integration;framework-regression;ActorGateway;actor")
  add_zlink_framework_test(
    test_cpp_framework_registry_topology
    tests/unit/test_cpp_framework_registry_topology.cpp
    "framework-integration;framework-regression;registry")
  add_zlink_framework_test(
    test_cpp_framework_monitoring
    tests/unit/test_cpp_framework_monitoring.cpp
    "framework-unit;framework-integration;monitoring")
  add_zlink_framework_test(
    test_cpp_framework_execution
    tests/unit/test_cpp_framework_execution.cpp
    "framework-unit;framework-regression;execution")
  add_zlink_framework_test(
    test_cpp_framework_messaging
    tests/unit/test_cpp_framework_messaging.cpp
    "framework-unit;framework-regression;messaging")
  add_zlink_framework_test(
    test_cpp_framework_module_hosted
    tests/unit/test_cpp_framework_module_hosted.cpp
    "framework-unit;framework-integration;module;hosted")
  add_executable(test_cpp_framework_gtest_harness
    tests/unit/test_cpp_framework_gtest_harness.cpp)
  target_link_libraries(test_cpp_framework_gtest_harness PRIVATE
    zlink::framework
    GTest::gtest_main
    GTest::gmock)
  add_test(NAME test_cpp_framework_gtest_harness
    COMMAND test_cpp_framework_gtest_harness)
  set_tests_properties(test_cpp_framework_gtest_harness PROPERTIES
    LABELS "framework-unit;framework-regression;gtest"
    TIMEOUT 30)
  add_executable(test_cpp_framework_extensions
    tests/unit/test_cpp_framework_extensions.cpp)
  target_link_libraries(test_cpp_framework_extensions PRIVATE
    zlink::framework_extension_metrics
    zlink::framework_extension_tracing
    zlink::framework_extension_kafka_bridge
    zlink::framework_extension_grpc_bridge
    zlink::framework_extension_http_gateway
    zlink::framework_extension_advanced_retry
    zlink::framework_extension_dead_letter_storage
    zlink::framework_extension_flatbuffers
    zlink::framework_extension_yaml_config
    zlink::framework_extension_custom_codec
    zlink::framework_extension_custom_transport
    GTest::gtest
    GTest::gmock)
  add_test(NAME test_cpp_framework_extensions
    COMMAND test_cpp_framework_extensions)
  set_tests_properties(test_cpp_framework_extensions PROPERTIES
    LABELS "framework-extension"
    TIMEOUT 30)
  add_zlink_connector_test(
    test_cpp_stream_connector
    tests/unit/test_cpp_stream_connector.cpp
    "connector-unit;connector-integration")
  target_link_libraries(test_cpp_stream_connector PRIVATE
    zlink::stream_connector_codecs)

  add_executable(test_unreal_stream_connector
    tests/unit/test_unreal_stream_connector.cpp)
  target_link_libraries(test_unreal_stream_connector PRIVATE
    zlink_unreal_stream_connector
    GTest::gtest
    GTest::gmock)
  add_test(NAME test_unreal_stream_connector COMMAND test_unreal_stream_connector)
  set_tests_properties(test_unreal_stream_connector PROPERTIES
    LABELS "unreal-connector-compile;unreal-connector-smoke"
    TIMEOUT 30)
endif()

if(ZLINK_FRAMEWORK_CPP_BUILD_SAMPLES)
  function(add_zlink_framework_sample target_name source_file label)
    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PRIVATE ${ARGN})
    add_test(NAME sample_smoke_${target_name} COMMAND ${target_name})
    set_tests_properties(sample_smoke_${target_name} PROPERTIES
      LABELS "${label};framework-sample-smoke"
      TIMEOUT 30)
  endfunction()

  add_zlink_framework_sample(
    sample_cpp_framework_bingo_registry
    samples/Bingo/Server/Registry/main.cpp
    "framework-sample-bingo;framework-sample-registry"
    zlink::framework)
  add_zlink_framework_sample(
    sample_cpp_framework_bingo_api
    samples/Bingo/Server/Api/main.cpp
    "framework-sample-bingo;framework-sample-api"
    zlink::framework)
  add_zlink_framework_sample(
    sample_cpp_framework_bingo_play
    samples/Bingo/Server/Play/main.cpp
    "framework-sample-bingo;framework-sample-play"
    zlink::framework)
  add_zlink_framework_sample(
    sample_cpp_framework_bingo_session
    samples/Bingo/Server/Session/main.cpp
    "framework-sample-bingo;framework-sample-session"
    zlink::framework
    zlink::stream_connector)
  add_zlink_framework_sample(
    sample_cpp_framework_bingo_client
    samples/Bingo/Client/main.cpp
    "framework-sample-bingo;framework-sample-client"
    zlink::framework
    zlink::stream_connector)

  add_zlink_framework_sample(
    sample_cpp_framework_tictactoe_registry
    samples/TicTacToe/Server/Registry/main.cpp
    "framework-sample-tictactoe;framework-sample-registry"
    zlink::framework
    zlink::stream_connector)
  add_zlink_framework_sample(
    sample_cpp_framework_tictactoe_api
    samples/TicTacToe/Server/Api/main.cpp
    "framework-sample-tictactoe;framework-sample-api"
    zlink::framework
    zlink::stream_connector)
  add_zlink_framework_sample(
    sample_cpp_framework_tictactoe_play
    samples/TicTacToe/Server/Play/main.cpp
    "framework-sample-tictactoe;framework-sample-play"
    zlink::framework
    zlink::stream_connector)
  add_zlink_framework_sample(
    sample_cpp_framework_tictactoe_session
    samples/TicTacToe/Server/Session/main.cpp
    "framework-sample-tictactoe;framework-sample-session"
    zlink::framework
    zlink::stream_connector)
  add_zlink_framework_sample(
    sample_cpp_framework_tictactoe_client
    samples/TicTacToe/Client/main.cpp
    "framework-sample-tictactoe;framework-sample-client"
    zlink::framework
    zlink::stream_connector)

  set_tests_properties(
    sample_smoke_sample_cpp_framework_bingo_client
    PROPERTIES
      LABELS "framework-sample-bingo;framework-sample-client;framework-sample-smoke;framework-sample-client-e2e")
  set_tests_properties(
    sample_smoke_sample_cpp_framework_tictactoe_client
    PROPERTIES
      LABELS "framework-sample-tictactoe;framework-sample-client;framework-sample-smoke;framework-sample-client-e2e")
endif()
