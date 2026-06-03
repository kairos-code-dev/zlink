if(NOT DEFINED ZLINK_FRAMEWORK_CPP_BUILD_DIR)
  message(FATAL_ERROR "ZLINK_FRAMEWORK_CPP_BUILD_DIR is required")
endif()
if(NOT DEFINED ZLINK_FRAMEWORK_CPP_INSTALL_PREFIX)
  message(FATAL_ERROR "ZLINK_FRAMEWORK_CPP_INSTALL_PREFIX is required")
endif()

set(consumer_source_dir
  "${ZLINK_FRAMEWORK_CPP_BUILD_DIR}/package-consumer-src")
set(consumer_build_dir
  "${ZLINK_FRAMEWORK_CPP_BUILD_DIR}/package-consumer-build")

file(REMOVE_RECURSE
  "${ZLINK_FRAMEWORK_CPP_INSTALL_PREFIX}"
  "${consumer_source_dir}"
  "${consumer_build_dir}")
file(MAKE_DIRECTORY "${consumer_source_dir}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${ZLINK_FRAMEWORK_CPP_BUILD_DIR}"
          --prefix "${ZLINK_FRAMEWORK_CPP_INSTALL_PREFIX}"
  RESULT_VARIABLE install_result)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "C++ framework install failed")
endif()

file(WRITE "${consumer_source_dir}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.20)
project(zlink_framework_cpp_consumer LANGUAGES CXX)

find_package(zlink_framework_cpp CONFIG REQUIRED)
find_package(zlink_stream_connector_cpp CONFIG REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE
  zlink::framework
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
  zlink::http_client
  zlink::stream_connector
  zlink::stream_connector_codecs)
]=])

file(WRITE "${consumer_source_dir}/main.cpp" [=[
#include <zlink/framework.hpp>
#include <zlink/framework/extensions.hpp>
#include <zlink/http_client.hpp>
#include <zlink/stream_connector.hpp>
#include <zlink/stream_connector/codecs/auto_codec.hpp>

#include <nlohmann/json.hpp>

struct login_request_t
{
  static constexpr const char *packet_name = "LoginRequest";
};

void
to_json (nlohmann::json &json, const login_request_t &)
{
  json = nlohmann::json::object ();
}

int
main ()
{
  auto app = zlink::framework::app_t::create ();
  (void) app;
  auto client = zlink::http_client::client_t::create ()
                  .base_url ("http://127.0.0.1:18080")
                  .json ()
                  .build ();
  (void) client;
  auto extensions = zlink::framework::extensions::known_extensions ();
  if (extensions.size () != 11) {
    return 2;
  }
  auto packet =
    zlink::stream_connector::codecs::encode_packet (login_request_t {});
  return packet.name == login_request_t::packet_name ? 0 : 1;
}
]=])

execute_process(
  COMMAND "${CMAKE_COMMAND}" -S "${consumer_source_dir}" -B "${consumer_build_dir}"
          "-DCMAKE_PREFIX_PATH=${ZLINK_FRAMEWORK_CPP_INSTALL_PREFIX}"
  RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "installed C++ framework consumer configure failed")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${consumer_build_dir}"
  RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "installed C++ framework consumer build failed")
endif()

if(WIN32)
  set(runtime_path
    "PATH=${ZLINK_FRAMEWORK_CPP_INSTALL_PREFIX}/bin;$ENV{PATH}")
elseif(APPLE)
  set(runtime_path
    "DYLD_LIBRARY_PATH=${ZLINK_FRAMEWORK_CPP_INSTALL_PREFIX}/lib:$ENV{DYLD_LIBRARY_PATH}")
else()
  set(runtime_path
    "LD_LIBRARY_PATH=${ZLINK_FRAMEWORK_CPP_INSTALL_PREFIX}/lib:$ENV{LD_LIBRARY_PATH}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "${runtime_path}"
          "${consumer_build_dir}/consumer"
  RESULT_VARIABLE run_result)
if(NOT run_result EQUAL 0)
  message(FATAL_ERROR "installed C++ framework consumer run failed")
endif()
