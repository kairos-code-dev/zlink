cmake_minimum_required(VERSION 3.10)
project(zlink_codec_protobuf LANGUAGES CXX)

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

find_package(Protobuf REQUIRED)

add_library(zlink-codec-protobuf INTERFACE)
add_library(zlink_cpp_codec_protobuf ALIAS zlink-codec-protobuf)
add_library(zlink::cpp_codec_protobuf ALIAS zlink-codec-protobuf)
target_compile_features(zlink-codec-protobuf INTERFACE cxx_std_20)

target_link_libraries(zlink-codec-protobuf INTERFACE zlink_cpp)

target_include_directories(zlink-codec-protobuf INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/include
)

if(TARGET protobuf::libprotobuf)
  target_link_libraries(zlink-codec-protobuf INTERFACE protobuf::libprotobuf)
elseif(TARGET protobuf::protobuf)
  target_link_libraries(zlink-codec-protobuf INTERFACE protobuf::protobuf)
else()
  message(FATAL_ERROR "Could not find a usable Protobuf CMake target")
endif()
