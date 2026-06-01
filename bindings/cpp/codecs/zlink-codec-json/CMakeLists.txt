cmake_minimum_required(VERSION 3.10)
project(zlink_codec_json LANGUAGES CXX)

set(
  ZLINK_CPP_DIR
  "${CMAKE_CURRENT_LIST_DIR}/../.."
  CACHE PATH
  "Path to the zlink C++ binding root"
)
set(
  ZLINK_CORE_DIR
  "${CMAKE_CURRENT_LIST_DIR}/../../../core"
  CACHE PATH
  "Path to the zlink core root"
)

find_package(nlohmann_json REQUIRED)

add_library(zlink-codec-json INTERFACE)
add_library(zlink_cpp_codec_json ALIAS zlink-codec-json)
add_library(zlink::cpp_codec_json ALIAS zlink-codec-json)
target_compile_features(zlink-codec-json INTERFACE cxx_std_20)

target_link_libraries(zlink-codec-json INTERFACE zlink_cpp)

target_include_directories(zlink-codec-json INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/include
)
target_link_libraries(zlink-codec-json INTERFACE nlohmann_json::nlohmann_json)
