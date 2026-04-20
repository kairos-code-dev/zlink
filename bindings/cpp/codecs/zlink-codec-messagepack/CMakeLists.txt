cmake_minimum_required(VERSION 3.10)
project(zlink_codec_messagepack LANGUAGES CXX)

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

find_package(msgpack-cxx REQUIRED CONFIG)

add_library(zlink-codec-messagepack INTERFACE)
target_compile_features(zlink-codec-messagepack INTERFACE cxx_std_17)

if(TARGET zlink-cpp)
  target_link_libraries(zlink-codec-messagepack INTERFACE zlink-cpp)
else()
  target_include_directories(zlink-codec-messagepack INTERFACE
    ${ZLINK_CPP_DIR}/include
    ${ZLINK_CORE_DIR}/include
  )
endif()

target_include_directories(zlink-codec-messagepack INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/include
)

if(TARGET msgpack-cxx)
  target_link_libraries(zlink-codec-messagepack INTERFACE msgpack-cxx)
elseif(TARGET msgpack-cxx::msgpack-cxx)
  target_link_libraries(zlink-codec-messagepack INTERFACE msgpack-cxx::msgpack-cxx)
else()
  message(FATAL_ERROR "Could not find a usable msgpack-cxx CMake target")
endif()
