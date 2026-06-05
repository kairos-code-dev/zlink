cmake_minimum_required(VERSION 3.20)
project(zlink_framework_cpp LANGUAGES C CXX)

include(GNUInstallDirs)
include(FetchContent)
include(CMakePackageConfigHelpers)
find_package(Threads REQUIRED)

option(ZLINK_FRAMEWORK_CPP_BUILD_TESTS "Build C++ framework tests" ON)
option(ZLINK_FRAMEWORK_CPP_BUILD_SAMPLES "Build C++ framework samples" ON)
option(ZLINK_STREAM_CONNECTOR_WITH_MESSAGEPACK "Enable Stream Connector MessagePack helpers" OFF)
option(ZLINK_STREAM_CONNECTOR_WITH_PROTOBUF "Enable Stream Connector Protobuf helpers" OFF)
option(ZLINK_STREAM_CONNECTOR_WITH_LZ4 "Enable Stream Connector LZ4 compression" ON)
option(ZLINK_FRAMEWORK_CPP_ENABLE_COVERAGE "Enable C++ framework gcov coverage instrumentation" OFF)
set(ZLINK_FRAMEWORK_CPP_COVERAGE_THRESHOLD 80 CACHE STRING
  "Minimum C++ framework runtime line coverage percentage")

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
  framework/src/runtime/configuration/builders/configuration_builder.cpp
  framework/src/runtime/configuration/services.cpp
  framework/src/runtime/codecs/serializer.cpp
  framework/src/runtime/diagnostics/logging.cpp
  framework/src/runtime/diagnostics/health.cpp
  framework/src/runtime/diagnostics/monitoring_runtime.cpp
  framework/src/runtime/dispatch/coroutine_executor.cpp
  framework/src/runtime/dispatch/offload_executor.cpp
  framework/src/runtime/execution/serial_execution_queue.cpp
  framework/src/runtime/handlers/handler_registry.cpp
  framework/src/runtime/dispatch/projection.cpp
  framework/src/runtime/host/app.cpp
  framework/src/runtime/http/http_host_service.cpp
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
find_package(Boost QUIET)
if(Boost_FOUND AND Boost_INCLUDE_DIRS)
  target_include_directories(zlink_framework PRIVATE ${Boost_INCLUDE_DIRS})
elseif(Boost_FOUND AND Boost_INCLUDE_DIR)
  target_include_directories(zlink_framework PRIVATE ${Boost_INCLUDE_DIR})
elseif(EXISTS "${ZLINK_FRAMEWORK_CPP_REPO_ROOT}/core/external/boost/boost/asio.hpp")
  target_include_directories(zlink_framework PRIVATE
    "${ZLINK_FRAMEWORK_CPP_REPO_ROOT}/core/external/boost")
else()
  message(FATAL_ERROR
    "Boost headers were not found. Install Boost, configure with vcpkg, "
    "or make sure core/external/boost exists in the repository checkout.")
endif()
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
  FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(nlohmann_json)
endif()
find_package(OpenSSL QUIET)
target_include_directories(zlink_framework PUBLIC
  $<BUILD_INTERFACE:${ZLINK_FRAMEWORK_CPP_REPO_ROOT}/bindings/cpp/codecs/zlink-codec-json/include>)
target_link_libraries(zlink_framework PUBLIC nlohmann_json::nlohmann_json)
if(OpenSSL_FOUND)
  target_compile_definitions(zlink_framework PRIVATE
    ZLINK_FRAMEWORK_HTTP_WITH_OPENSSL)
  target_link_libraries(zlink_framework PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

add_library(zlink_http_client STATIC
  http-client/src/client.cpp
  http-client/src/runtime/http_client_runtime.cpp)
add_library(zlink::http_client ALIAS zlink_http_client)
set_target_properties(zlink_http_client PROPERTIES EXPORT_NAME http_client)
target_compile_features(zlink_http_client PUBLIC cxx_std_20)
target_include_directories(zlink_http_client PUBLIC
  $<BUILD_INTERFACE:${ZLINK_FRAMEWORK_CPP_DIR}/http-client/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_include_directories(zlink_http_client PRIVATE
  ${ZLINK_FRAMEWORK_CPP_DIR}/http-client/src)
target_link_libraries(zlink_http_client PUBLIC
  zlink::framework
  nlohmann_json::nlohmann_json)
target_link_libraries(zlink_http_client PRIVATE Threads::Threads)
if(Boost_FOUND AND Boost_INCLUDE_DIRS)
  target_include_directories(zlink_http_client PRIVATE ${Boost_INCLUDE_DIRS})
elseif(Boost_FOUND AND Boost_INCLUDE_DIR)
  target_include_directories(zlink_http_client PRIVATE ${Boost_INCLUDE_DIR})
elseif(EXISTS "${ZLINK_FRAMEWORK_CPP_REPO_ROOT}/core/external/boost/boost/asio.hpp")
  target_include_directories(zlink_http_client PRIVATE
    "${ZLINK_FRAMEWORK_CPP_REPO_ROOT}/core/external/boost")
endif()
if(OpenSSL_FOUND)
  target_compile_definitions(zlink_http_client PRIVATE
    ZLINK_HTTP_CLIENT_WITH_OPENSSL)
  target_link_libraries(zlink_http_client PRIVATE OpenSSL::SSL OpenSSL::Crypto)
  set(ZLINK_HTTP_CLIENT_EXPORT_OPENSSL_DEPENDENCY
    "find_dependency(OpenSSL)")
else()
  set(ZLINK_HTTP_CLIENT_EXPORT_OPENSSL_DEPENDENCY "")
endif()
find_program(ZLINK_OPENSSL_EXECUTABLE openssl)
if(OpenSSL_FOUND AND ZLINK_OPENSSL_EXECUTABLE)
  set(ZLINK_HTTP_CLIENT_TEST_CERT_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/http-client-test-certs")
  set(ZLINK_HTTP_CLIENT_TEST_CERT
    "${ZLINK_HTTP_CLIENT_TEST_CERT_DIR}/server.crt")
  set(ZLINK_HTTP_CLIENT_TEST_KEY
    "${ZLINK_HTTP_CLIENT_TEST_CERT_DIR}/server.key")
  file(MAKE_DIRECTORY "${ZLINK_HTTP_CLIENT_TEST_CERT_DIR}")
  execute_process(
    COMMAND ${ZLINK_OPENSSL_EXECUTABLE}
      req -x509 -newkey rsa:2048 -nodes -days 7
      -subj /CN=localhost
      -addext subjectAltName=DNS:localhost
      -keyout "${ZLINK_HTTP_CLIENT_TEST_KEY}"
      -out "${ZLINK_HTTP_CLIENT_TEST_CERT}"
    RESULT_VARIABLE ZLINK_HTTP_CLIENT_CERT_RESULT
    OUTPUT_QUIET
    ERROR_QUIET)
  if(NOT ZLINK_HTTP_CLIENT_CERT_RESULT EQUAL 0)
    message(WARNING
      "Could not generate HTTP client HTTPS test certificate; HTTPS e2e test will be disabled.")
  endif()
  if(EXISTS "${ZLINK_HTTP_CLIENT_TEST_CERT}" AND
      EXISTS "${ZLINK_HTTP_CLIENT_TEST_KEY}")
    set(ZLINK_HTTP_CLIENT_TEST_WITH_OPENSSL ON)
  endif()
endif()

add_library(zlink_stream_connector STATIC
  connector/src/runtime/calls/zlink_stream_calls.cpp
  connector/src/runtime/connector_lifecycle.cpp
  connector/src/runtime/connector_runtime.cpp
  connector/src/runtime/heartbeat_monitor.cpp
  connector/src/runtime/protocol/compression/lz4_compression_codec.cpp
  connector/src/runtime/protocol/framing/frame_codec.cpp
  connector/src/runtime/protocol/framing.cpp
  connector/src/runtime/protocol/header_codec.cpp
  connector/src/runtime/protocol/metadata_codec.cpp
  connector/src/runtime/protocol/packet_name_resolver.cpp
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
if(OpenSSL_FOUND)
  target_compile_definitions(zlink_stream_connector PRIVATE
    ZLINK_STREAM_CONNECTOR_WITH_OPENSSL)
  target_link_libraries(zlink_stream_connector PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto)
  set(ZLINK_STREAM_CONNECTOR_EXPORT_OPENSSL_DEPENDENCY
    "find_dependency(OpenSSL)")
else()
  set(ZLINK_STREAM_CONNECTOR_EXPORT_OPENSSL_DEPENDENCY "")
endif()
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
set(ZLINK_STREAM_CONNECTOR_MESSAGEPACK_ENABLED OFF)
if(ZLINK_STREAM_CONNECTOR_WITH_MESSAGEPACK AND TARGET zlink::cpp_codec_messagepack)
  set(ZLINK_STREAM_CONNECTOR_MESSAGEPACK_ENABLED ON)
endif()
if(ZLINK_STREAM_CONNECTOR_MESSAGEPACK_ENABLED)
  set(ZLINK_STREAM_CONNECTOR_EXPORT_MESSAGEPACK_DEPENDENCY
    "find_dependency(msgpack-cxx CONFIG)")
else()
  set(ZLINK_STREAM_CONNECTOR_EXPORT_MESSAGEPACK_DEPENDENCY "")
endif()
set(ZLINK_STREAM_CONNECTOR_PROTOBUF_ENABLED OFF)
if(ZLINK_STREAM_CONNECTOR_WITH_PROTOBUF AND TARGET zlink::cpp_codec_protobuf)
  set(ZLINK_STREAM_CONNECTOR_PROTOBUF_ENABLED ON)
endif()
if(ZLINK_STREAM_CONNECTOR_PROTOBUF_ENABLED)
  set(ZLINK_STREAM_CONNECTOR_EXPORT_PROTOBUF_DEPENDENCY
    "find_dependency(Protobuf)")
else()
  set(ZLINK_STREAM_CONNECTOR_EXPORT_PROTOBUF_DEPENDENCY "")
endif()
target_compile_definitions(zlink_stream_connector PRIVATE
  $<$<BOOL:${ZLINK_STREAM_CONNECTOR_MESSAGEPACK_ENABLED}>:ZLINK_STREAM_CONNECTOR_WITH_MESSAGEPACK>
  $<$<BOOL:${ZLINK_STREAM_CONNECTOR_PROTOBUF_ENABLED}>:ZLINK_STREAM_CONNECTOR_WITH_PROTOBUF>
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
if(ZLINK_STREAM_CONNECTOR_MESSAGEPACK_ENABLED)
  target_link_libraries(zlink_stream_connector_codecs INTERFACE
    zlink::cpp_codec_messagepack)
endif()
if(ZLINK_STREAM_CONNECTOR_PROTOBUF_ENABLED)
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

function(zlink_enable_coverage target_name)
  if(ZLINK_FRAMEWORK_CPP_ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(${target_name} PRIVATE -O0 -g --coverage)
      target_link_options(${target_name} PUBLIC --coverage)
    else()
      message(FATAL_ERROR
        "ZLINK_FRAMEWORK_CPP_ENABLE_COVERAGE requires GCC or Clang compatible coverage flags.")
    endif()
  endif()
endfunction()

zlink_enable_coverage(zlink_framework)
zlink_enable_coverage(zlink_http_client)
zlink_enable_coverage(zlink_stream_connector)
zlink_enable_coverage(zlink_unreal_stream_connector)

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
  zlink_http_client
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
install(DIRECTORY http-client/include/
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
  elseif(UNIX AND NOT APPLE)
    get_filename_component(
      ZLINK_FRAMEWORK_CPP_NATIVE_RUNTIME_DIR
      "${ZLINK_FRAMEWORK_CPP_NATIVE_RUNTIME}"
      DIRECTORY)
    file(GLOB ZLINK_FRAMEWORK_CPP_NATIVE_RUNTIME_FILES
      "${ZLINK_FRAMEWORK_CPP_NATIVE_RUNTIME_DIR}/libzlink.so*")
    install(FILES ${ZLINK_FRAMEWORK_CPP_NATIVE_RUNTIME_FILES}
      DESTINATION ${CMAKE_INSTALL_LIBDIR})
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

  function(add_zlink_http_client_test target_name source_file labels)
    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PRIVATE zlink::http_client)
    target_link_zlink_test_harness(${target_name})
    target_include_directories(${target_name} PRIVATE
      ${ZLINK_FRAMEWORK_CPP_DIR}/http-client/src)
    add_test(NAME ${target_name} COMMAND ${target_name})
    set_tests_properties(${target_name} PROPERTIES
      LABELS "${labels}"
      TIMEOUT 30)
  endfunction()

  add_executable(test_cpp_framework_contract_headers
    tests/Zlink.Framework.ContractTests/test_cpp_framework_contract_headers.cpp)
  target_link_libraries(test_cpp_framework_contract_headers PRIVATE
    zlink::framework
    zlink::framework_extension_metrics
    zlink::http_client
    zlink::stream_connector
    GTest::gtest
    GTest::gmock)
  add_test(NAME test_cpp_framework_contract_headers
    COMMAND test_cpp_framework_contract_headers)
  set_tests_properties(test_cpp_framework_contract_headers PROPERTIES
    LABELS "framework-contract"
    TIMEOUT 30)
  add_test(NAME test_cpp_framework_async_contract
    COMMAND test_cpp_framework_contract_headers)
  set_tests_properties(test_cpp_framework_async_contract PROPERTIES
    LABELS "framework-contract;framework-unit;async"
    TIMEOUT 30)

  add_executable(test_cpp_framework_sample_parity
    tests/Zlink.Framework.ContractTests/test_cpp_framework_sample_parity.cpp)
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
  add_test(NAME test_cpp_framework_parity_contract
    COMMAND test_cpp_framework_sample_parity)
  set_tests_properties(test_cpp_framework_parity_contract PROPERTIES
    LABELS "framework-regression;framework-sample-parity;parity"
    TIMEOUT 30)

  add_executable(test_cpp_framework_layout_contract
    tests/Zlink.Framework.ContractTests/test_cpp_framework_layout_contract.cpp)
  target_compile_definitions(test_cpp_framework_layout_contract PRIVATE
    ZLINK_FRAMEWORK_CPP_SOURCE_DIR="${ZLINK_FRAMEWORK_CPP_DIR}")
  target_compile_features(test_cpp_framework_layout_contract PRIVATE cxx_std_20)
  target_link_zlink_test_harness(test_cpp_framework_layout_contract)
  add_test(NAME test_cpp_framework_layout_contract
    COMMAND test_cpp_framework_layout_contract)
  set_tests_properties(test_cpp_framework_layout_contract PROPERTIES
    LABELS "framework-contract"
    TIMEOUT 30)
  add_test(
    NAME test_cpp_framework_tooling_contract
    COMMAND ${CMAKE_COMMAND}
      -D ZLINK_FRAMEWORK_CPP_SOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}
      -D ZLINK_FRAMEWORK_CPP_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
      -P ${CMAKE_CURRENT_LIST_DIR}/tests/Zlink.Framework.ContractTests/verify_tooling_contract.cmake)
  set_tests_properties(test_cpp_framework_tooling_contract PROPERTIES
    LABELS "framework-contract;framework-package;framework-tooling"
    TIMEOUT 30)
  add_test(
    NAME test_cpp_framework_label_contract
    COMMAND ${CMAKE_COMMAND}
      -D ZLINK_FRAMEWORK_CPP_SOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}
      -D ZLINK_FRAMEWORK_CPP_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
      -D ZLINK_FRAMEWORK_CPP_EXPECT_COVERAGE_LABEL=${ZLINK_FRAMEWORK_CPP_ENABLE_COVERAGE}
      -P ${CMAKE_CURRENT_LIST_DIR}/tests/Zlink.Framework.ContractTests/verify_ctest_label_contract.cmake)
  set_tests_properties(test_cpp_framework_label_contract PROPERTIES
    LABELS "framework-contract;framework-regression"
    TIMEOUT 30)

  add_zlink_framework_test(
    test_cpp_framework_app_host
    tests/Zlink.Framework.UnitTests/test_cpp_framework_app_host.cpp
    "framework-unit;framework-host;framework-config;framework-http;framework-http-e2e")
  target_link_libraries(test_cpp_framework_app_host PRIVATE zlink::http_client)
  if(ZLINK_FRAMEWORK_CPP_ENABLE_COVERAGE)
    set(ZLINK_FRAMEWORK_HTTP_TEST_HTTP_ENDPOINT "http://127.0.0.1:18180")
    set(ZLINK_FRAMEWORK_HTTP_TEST_HTTPS_INVALID_ENDPOINT "https://127.0.0.1:18543")
    set(ZLINK_FRAMEWORK_HTTP_TEST_HTTPS_ENDPOINT "https://127.0.0.1:18544")
    set(ZLINK_FRAMEWORK_HTTP_TEST_HTTPS_CLIENT_BASE_URL "https://localhost:18544")
  else()
    set(ZLINK_FRAMEWORK_HTTP_TEST_HTTP_ENDPOINT "http://127.0.0.1:18080")
    set(ZLINK_FRAMEWORK_HTTP_TEST_HTTPS_INVALID_ENDPOINT "https://127.0.0.1:18443")
    set(ZLINK_FRAMEWORK_HTTP_TEST_HTTPS_ENDPOINT "https://127.0.0.1:18444")
    set(ZLINK_FRAMEWORK_HTTP_TEST_HTTPS_CLIENT_BASE_URL "https://localhost:18444")
  endif()
  target_compile_definitions(test_cpp_framework_app_host PRIVATE
    ZLINK_FRAMEWORK_HTTP_TEST_HTTP_ENDPOINT="${ZLINK_FRAMEWORK_HTTP_TEST_HTTP_ENDPOINT}"
    ZLINK_FRAMEWORK_HTTP_TEST_HTTPS_INVALID_ENDPOINT="${ZLINK_FRAMEWORK_HTTP_TEST_HTTPS_INVALID_ENDPOINT}"
    ZLINK_FRAMEWORK_HTTP_TEST_HTTPS_ENDPOINT="${ZLINK_FRAMEWORK_HTTP_TEST_HTTPS_ENDPOINT}"
    ZLINK_FRAMEWORK_HTTP_TEST_HTTPS_CLIENT_BASE_URL="${ZLINK_FRAMEWORK_HTTP_TEST_HTTPS_CLIENT_BASE_URL}")
  if(ZLINK_HTTP_CLIENT_TEST_WITH_OPENSSL)
    target_compile_definitions(test_cpp_framework_app_host PRIVATE
      ZLINK_FRAMEWORK_HTTP_TEST_WITH_OPENSSL
      ZLINK_FRAMEWORK_HTTP_TEST_CERT="${ZLINK_HTTP_CLIENT_TEST_CERT}"
      ZLINK_FRAMEWORK_HTTP_TEST_KEY="${ZLINK_HTTP_CLIENT_TEST_KEY}")
  endif()
  add_test(NAME test_cpp_framework_http_integration
    COMMAND test_cpp_framework_app_host)
  set_tests_properties(test_cpp_framework_http_integration PROPERTIES
    LABELS "framework-integration;framework-http;framework-http-e2e;http"
    TIMEOUT 30)
  set_tests_properties(
    test_cpp_framework_app_host
    test_cpp_framework_http_integration
    PROPERTIES
      RESOURCE_LOCK zlink_cpp_framework_http_ports)
  add_zlink_framework_test(
    test_cpp_framework_DI_scope
    tests/Zlink.Framework.UnitTests/test_cpp_framework_di.cpp
    "framework-unit;framework-regression;DI;scope")
  add_zlink_framework_test(
    test_cpp_framework_runtime_integration
    tests/Zlink.Framework.UnitTests/test_cpp_framework_runtime_integration.cpp
    "framework-unit;framework-integration;runtime")
  add_zlink_framework_test(
    test_cpp_framework_serializer_registry
    tests/Zlink.Framework.UnitTests/test_cpp_framework_serializer_registry.cpp
    "framework-unit;serializer")
  add_zlink_framework_test(
    test_cpp_framework_handler_registry
    tests/Zlink.Framework.UnitTests/test_cpp_framework_handler_registry.cpp
    "framework-unit;handler")
  add_zlink_framework_test(
    test_cpp_framework_channel_messaging
    tests/Zlink.Framework.UnitTests/test_cpp_framework_channel_messaging.cpp
    "framework-integration;framework-regression;framework-zlink;framework-zlink-channel;channel")
  add_zlink_framework_test(
    test_cpp_framework_backpressure_reliability
    tests/Zlink.Framework.UnitTests/test_cpp_framework_backpressure_reliability.cpp
    "framework-unit;framework-regression;backpressure;reliability")
  add_zlink_framework_test(
    test_cpp_framework_spot_runtime
    tests/Zlink.Framework.UnitTests/test_cpp_framework_spot_runtime.cpp
    "framework-integration;framework-regression;framework-zlink;framework-zlink-spot;spot")
  add_zlink_framework_test(
    test_cpp_framework_spot_timer
    tests/Zlink.Framework.UnitTests/test_cpp_framework_spot_timer.cpp
    "framework-unit;framework-integration;framework-regression;framework-zlink;framework-zlink-spot;timer")
  add_zlink_framework_test(
    test_cpp_framework_stream_framework
    tests/Zlink.Framework.UnitTests/test_cpp_framework_stream_framework.cpp
    "framework-integration;framework-regression;framework-zlink;framework-zlink-stream;stream")
  add_zlink_framework_test(
    test_cpp_framework_ActorGateway_actor_session_relay
    tests/Zlink.Framework.UnitTests/test_cpp_framework_actor_gateway.cpp
    "framework-integration;framework-regression;framework-zlink;framework-zlink-actor-gateway;ActorGateway;actor")
  add_zlink_framework_test(
    test_cpp_framework_registry_topology
    tests/Zlink.Framework.UnitTests/test_cpp_framework_registry_topology.cpp
    "framework-integration;framework-regression;framework-zlink;framework-zlink-registry;registry")
  add_zlink_framework_test(
    test_cpp_framework_monitoring
    tests/Zlink.Framework.UnitTests/test_cpp_framework_monitoring.cpp
    "framework-unit;framework-integration;framework-observability;monitoring")
  add_zlink_framework_test(
    test_cpp_framework_execution
    tests/Zlink.Framework.UnitTests/test_cpp_framework_execution.cpp
    "framework-unit;framework-regression;execution")
  add_zlink_framework_test(
    test_cpp_framework_messaging
    tests/Zlink.Framework.UnitTests/test_cpp_framework_messaging.cpp
    "framework-unit;framework-regression;messaging")
  add_zlink_framework_test(
    test_cpp_framework_module_hosted
    tests/Zlink.Framework.UnitTests/test_cpp_framework_module_hosted.cpp
    "framework-unit;framework-integration;module;hosted")
  add_executable(test_cpp_framework_gtest_harness
    tests/Zlink.Framework.UnitTests/test_cpp_framework_gtest_harness.cpp)
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
    tests/Zlink.Framework.UnitTests/test_cpp_framework_extensions.cpp)
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
  add_test(NAME test_cpp_framework_install_consumer
    COMMAND
      ${CMAKE_COMMAND}
      -DZLINK_FRAMEWORK_CPP_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
      -DZLINK_FRAMEWORK_CPP_INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}/package-consumer-install
      -P ${CMAKE_CURRENT_LIST_DIR}/tests/Zlink.Framework.PackageTests/install_consumer.cmake)
  set_tests_properties(test_cpp_framework_install_consumer PROPERTIES
    LABELS "framework-package;framework-regression;connector-package"
    TIMEOUT 120)
  add_zlink_connector_test(
    test_cpp_stream_connector
    tests/Systems.Zlink.Stream.Connector.Tests/test_cpp_stream_connector.cpp
    "connector-unit;connector-integration;connector-e2e;connector-contract;connector-protocol;connector-transport;connector-typed")
  target_link_libraries(test_cpp_stream_connector PRIVATE
    zlink::stream_connector_codecs)
  if(ZLINK_HTTP_CLIENT_TEST_WITH_OPENSSL)
    target_compile_definitions(test_cpp_stream_connector PRIVATE
      ZLINK_STREAM_CONNECTOR_TEST_WITH_OPENSSL
      ZLINK_STREAM_CONNECTOR_TEST_CERT="${ZLINK_HTTP_CLIENT_TEST_CERT}"
      ZLINK_STREAM_CONNECTOR_TEST_KEY="${ZLINK_HTTP_CLIENT_TEST_KEY}")
    target_link_libraries(test_cpp_stream_connector PRIVATE
      OpenSSL::SSL
      OpenSSL::Crypto)
  endif()
  add_zlink_http_client_test(
    test_cpp_http_client
    tests/Zlink.HttpClient.Tests/test_cpp_http_client.cpp
    "http-client-contract;http-client-unit;http-client-e2e;http-client-regression")
  if(ZLINK_HTTP_CLIENT_TEST_WITH_OPENSSL)
    target_compile_definitions(test_cpp_http_client PRIVATE
      ZLINK_HTTP_CLIENT_TEST_WITH_OPENSSL
      ZLINK_HTTP_CLIENT_TEST_CERT="${ZLINK_HTTP_CLIENT_TEST_CERT}"
      ZLINK_HTTP_CLIENT_TEST_KEY="${ZLINK_HTTP_CLIENT_TEST_KEY}")
    target_link_libraries(test_cpp_http_client PRIVATE
      OpenSSL::SSL
      OpenSSL::Crypto)
    set_tests_properties(test_cpp_http_client PROPERTIES
      LABELS "http-client-contract;http-client-unit;http-client-e2e;http-client-https;http-client-regression")
  endif()

  add_executable(test_unreal_stream_connector
    tests/Zlink.Unreal.Stream.Connector.Tests/test_unreal_stream_connector.cpp)
  target_link_libraries(test_unreal_stream_connector PRIVATE
    zlink_unreal_stream_connector
    GTest::gtest
    GTest::gmock)
  add_test(NAME test_unreal_stream_connector COMMAND test_unreal_stream_connector)
  set_tests_properties(test_unreal_stream_connector PROPERTIES
    LABELS "unreal-connector-contract;unreal-connector-compile;unreal-connector-smoke"
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
  add_executable(sample_cpp_framework_bingo_client
    samples/Bingo/Client/main.cpp)
  target_link_libraries(sample_cpp_framework_bingo_client PRIVATE
    zlink::framework
    zlink::stream_connector
    zlink::stream_connector_codecs)
  add_executable(sample_cpp_framework_bingo_e2e_server
    samples/Bingo/Server/E2E/main.cpp)
  target_link_libraries(sample_cpp_framework_bingo_e2e_server PRIVATE
    zlink::framework
    zlink::stream_connector
    zlink::stream_connector_codecs)

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
  add_executable(sample_cpp_framework_tictactoe_client
    samples/TicTacToe/Client/main.cpp)
  target_link_libraries(sample_cpp_framework_tictactoe_client PRIVATE
    zlink::framework
    zlink::http_client
    zlink::stream_connector
    zlink::stream_connector_codecs)
  add_executable(sample_cpp_framework_tictactoe_e2e_server
    samples/TicTacToe/Server/E2E/main.cpp)
  target_link_libraries(sample_cpp_framework_tictactoe_e2e_server PRIVATE
    zlink::framework
    zlink::http_client
    zlink::stream_connector
    zlink::stream_connector_codecs)

  add_test(
    NAME sample_process_e2e_sample_cpp_framework_bingo_client
    COMMAND ${CMAKE_CURRENT_LIST_DIR}/tests/Zlink.Framework.E2ETests/Samples/run_sample_process_e2e.sh
      $<TARGET_FILE:sample_cpp_framework_bingo_e2e_server>
      $<TARGET_FILE:sample_cpp_framework_bingo_client>
      ${CMAKE_CURRENT_BINARY_DIR}/sample-process-e2e-logs/bingo
      ${CMAKE_CURRENT_BINARY_DIR}/sample-process-e2e-logs/bingo/bingo-server.log
      "bind |monitor stream ready|monitor event stream_ready|recv AuthenticateReq|recv MatchBingoReq|recv StartBingoGameReq|recv LeaveRoomReq|reply AuthenticateReq|reply MatchBingoReq|push PlayerJoinedNotify|push BingoGameStartedNotify|push BingoNumberDrawnNotify|push BingoGameEndedNotify|actor relay player-1|disconnect client|shutdown server"
      10
      9
      8)
  set_tests_properties(
    sample_process_e2e_sample_cpp_framework_bingo_client
    PROPERTIES
      LABELS "framework-sample-bingo;framework-sample-client;framework-sample-e2e;framework-sample-client-e2e;framework-sample-log;framework-sample-process-e2e;connector-e2e;parity"
      TIMEOUT 30)

  add_test(
    NAME sample_process_e2e_sample_cpp_framework_tictactoe_client
    COMMAND ${CMAKE_CURRENT_LIST_DIR}/tests/Zlink.Framework.E2ETests/Samples/run_sample_process_e2e.sh
      $<TARGET_FILE:sample_cpp_framework_tictactoe_e2e_server>
      $<TARGET_FILE:sample_cpp_framework_tictactoe_client>
      ${CMAKE_CURRENT_BINARY_DIR}/sample-process-e2e-logs/tictactoe
      ${CMAKE_CURRENT_BINARY_DIR}/sample-process-e2e-logs/tictactoe/tictactoe-server.log
      "bind |monitor stream ready|monitor event stream_ready|http POST /games|recv CreateMatchReq|recv AuthenticateReq|recv JoinMatchReq|recv PlaceMarkReq|reply CreateMatchReq|reply AuthenticateReq|reply JoinMatchReq|reply PlaceMarkReq|push TurnChangedNotify|actor relay player-x|disconnect client|shutdown server"
      10
      10
      9)
  set_tests_properties(
    sample_process_e2e_sample_cpp_framework_tictactoe_client
    PROPERTIES
      LABELS "framework-sample-tictactoe;framework-sample-client;framework-sample-e2e;framework-sample-client-e2e;framework-sample-log;framework-sample-process-e2e;connector-e2e;parity"
      TIMEOUT 30)
endif()

if(ZLINK_FRAMEWORK_CPP_BUILD_TESTS AND ZLINK_FRAMEWORK_CPP_ENABLE_COVERAGE)
  find_program(ZLINK_GCOV_EXECUTABLE gcov REQUIRED)
  get_property(ZLINK_FRAMEWORK_CPP_TESTS DIRECTORY PROPERTY TESTS)
  add_test(
    NAME test_cpp_framework_coverage_threshold
    COMMAND ${CMAKE_COMMAND}
      -D ZLINK_FRAMEWORK_CPP_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
      -D ZLINK_FRAMEWORK_CPP_SOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}
      -D ZLINK_FRAMEWORK_CPP_COVERAGE_THRESHOLD=${ZLINK_FRAMEWORK_CPP_COVERAGE_THRESHOLD}
      -D ZLINK_GCOV_EXECUTABLE=${ZLINK_GCOV_EXECUTABLE}
      -P ${CMAKE_CURRENT_LIST_DIR}/tests/Zlink.Framework.Coverage/coverage_threshold.cmake)
  set_tests_properties(test_cpp_framework_coverage_threshold PROPERTIES
    LABELS "framework-coverage;framework-regression"
    DEPENDS "${ZLINK_FRAMEWORK_CPP_TESTS}"
    TIMEOUT 120)
endif()
