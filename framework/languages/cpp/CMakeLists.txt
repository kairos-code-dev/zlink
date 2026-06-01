cmake_minimum_required(VERSION 3.20)
project(zlink_framework_cpp LANGUAGES CXX)

include(GNUInstallDirs)

option(ZLINK_FRAMEWORK_CPP_BUILD_TESTS "Build C++ framework tests" ON)
option(ZLINK_FRAMEWORK_CPP_BUILD_SAMPLES "Build C++ framework samples" ON)
option(ZLINK_STREAM_CONNECTOR_WITH_JSON "Enable Stream Connector JSON helpers" ON)
option(ZLINK_STREAM_CONNECTOR_WITH_MESSAGEPACK "Enable Stream Connector MessagePack helpers" OFF)
option(ZLINK_STREAM_CONNECTOR_WITH_PROTOBUF "Enable Stream Connector Protobuf helpers" OFF)
option(ZLINK_STREAM_CONNECTOR_WITH_LZ4 "Enable Stream Connector LZ4 compression" OFF)

set(ZLINK_FRAMEWORK_CPP_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(ZLINK_FRAMEWORK_CPP_REPO_ROOT "${ZLINK_FRAMEWORK_CPP_DIR}/../../.." CACHE PATH
  "Path to the zlink repository root")

if(NOT TARGET zlink::cpp)
  set(ZLINK_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(ZLINK_CPP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
  add_subdirectory(
    "${ZLINK_FRAMEWORK_CPP_REPO_ROOT}/bindings/cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/bindings_cpp")
endif()

add_library(zlink_framework STATIC
  framework/src/runtime/channels/channel_runtime.cpp
  framework/src/runtime/actors/actor_gateway_runtime.cpp
  framework/src/runtime/configuration/configuration.cpp
  framework/src/runtime/configuration/services.cpp
  framework/src/runtime/codecs/serializer.cpp
  framework/src/runtime/diagnostics/logging.cpp
  framework/src/runtime/diagnostics/monitoring_runtime.cpp
  framework/src/runtime/dispatch/offload_executor.cpp
  framework/src/runtime/handlers/handler_registry.cpp
  framework/src/runtime/dispatch/projection.cpp
  framework/src/runtime/host/app.cpp
  framework/src/runtime/host/framework_runtime.cpp
  framework/src/runtime/registry/registry_runtime.cpp
  framework/src/runtime/spots/spot_runtime.cpp
  framework/src/runtime/streams/stream_runtime.cpp
  framework/src/runtime/timers/timer_runtime.cpp
  framework/src/runtime/transport/endpoint.cpp)
add_library(zlink::framework ALIAS zlink_framework)
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
  connector/src/runtime/connector_runtime.cpp)
add_library(zlink::stream_connector ALIAS zlink_stream_connector)
target_compile_features(zlink_stream_connector PUBLIC cxx_std_20)
target_include_directories(zlink_stream_connector PUBLIC
  $<BUILD_INTERFACE:${ZLINK_FRAMEWORK_CPP_DIR}/connector/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_include_directories(zlink_stream_connector PRIVATE
  ${ZLINK_FRAMEWORK_CPP_DIR}/connector/src)
target_link_libraries(zlink_stream_connector PUBLIC zlink::cpp)
target_compile_definitions(zlink_stream_connector PRIVATE
  $<$<BOOL:${ZLINK_STREAM_CONNECTOR_WITH_MESSAGEPACK}>:ZLINK_STREAM_CONNECTOR_WITH_MESSAGEPACK>
  $<$<BOOL:${ZLINK_STREAM_CONNECTOR_WITH_PROTOBUF}>:ZLINK_STREAM_CONNECTOR_WITH_PROTOBUF>
  $<$<BOOL:${ZLINK_STREAM_CONNECTOR_WITH_LZ4}>:ZLINK_STREAM_CONNECTOR_WITH_LZ4>)

add_library(zlink_unreal_stream_connector STATIC
  unreal-connector/Source/ZLinkStreamConnector/Private/ZLinkStreamConnector.cpp)
target_compile_features(zlink_unreal_stream_connector PUBLIC cxx_std_20)
target_include_directories(zlink_unreal_stream_connector PUBLIC
  ${ZLINK_FRAMEWORK_CPP_DIR}/unreal-connector/Source/ZLinkStreamConnector/Public)
target_include_directories(zlink_unreal_stream_connector PRIVATE
  ${ZLINK_FRAMEWORK_CPP_DIR}/connector/src)
target_link_libraries(zlink_unreal_stream_connector PUBLIC zlink::stream_connector)

function(add_zlink_framework_extension target_name alias_name)
  add_library(${target_name} INTERFACE)
  add_library(${alias_name} ALIAS ${target_name})
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
  zlink_stream_connector
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
install(DIRECTORY framework/include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.hpp")
install(DIRECTORY connector/include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.hpp")
install(DIRECTORY extensions/include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.hpp")
install(EXPORT zlink_framework_cppTargets
  NAMESPACE zlink::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zlink_framework_cpp)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  enable_testing()
endif()

if(ZLINK_FRAMEWORK_CPP_BUILD_TESTS)
  function(add_zlink_framework_test target_name source_file labels)
    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PRIVATE zlink::framework)
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
    zlink::stream_connector)
  add_test(NAME test_cpp_framework_contract_headers
    COMMAND test_cpp_framework_contract_headers)
  set_tests_properties(test_cpp_framework_contract_headers PROPERTIES
    LABELS "framework-contract"
    TIMEOUT 30)

  add_executable(test_cpp_framework_layout_contract
    tests/contract/test_cpp_framework_layout_contract.cpp)
  target_compile_definitions(test_cpp_framework_layout_contract PRIVATE
    ZLINK_FRAMEWORK_CPP_SOURCE_DIR="${ZLINK_FRAMEWORK_CPP_DIR}")
  target_compile_features(test_cpp_framework_layout_contract PRIVATE cxx_std_20)
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
    test_cpp_framework_module_hosted
    tests/unit/test_cpp_framework_module_hosted.cpp
    "framework-unit;framework-integration;module;hosted")
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
    zlink::framework_extension_custom_transport)
  add_test(NAME test_cpp_framework_extensions
    COMMAND test_cpp_framework_extensions)
  set_tests_properties(test_cpp_framework_extensions PROPERTIES
    LABELS "framework-extension"
    TIMEOUT 30)
  add_zlink_connector_test(
    test_cpp_stream_connector
    tests/unit/test_cpp_stream_connector.cpp
    "connector-unit;connector-integration")

  add_executable(test_unreal_stream_connector
    tests/unit/test_unreal_stream_connector.cpp)
  target_link_libraries(test_unreal_stream_connector PRIVATE
    zlink_unreal_stream_connector)
  add_test(NAME test_unreal_stream_connector COMMAND test_unreal_stream_connector)
  set_tests_properties(test_unreal_stream_connector PROPERTIES
    LABELS "unreal-connector-compile;unreal-connector-smoke"
    TIMEOUT 30)
endif()

if(ZLINK_FRAMEWORK_CPP_BUILD_SAMPLES)
  function(add_zlink_framework_sample target_name source_file linked_target label)
    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PRIVATE ${linked_target})
    add_test(NAME sample_smoke_${target_name} COMMAND ${target_name})
    set_tests_properties(sample_smoke_${target_name} PROPERTIES
      LABELS "${label};framework-sample-smoke"
      TIMEOUT 30)
  endfunction()

  add_zlink_framework_sample(
    sample_cpp_framework_bingo
    samples/Bingo/main.cpp
    zlink::framework
    "framework-sample-bingo")
  add_zlink_framework_sample(
    sample_cpp_framework_tictactoe
    samples/TicTacToe/main.cpp
    zlink::framework
    "framework-sample-tictactoe")
endif()
