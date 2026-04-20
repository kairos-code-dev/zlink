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
target_compile_features(zlink-codec-json INTERFACE cxx_std_17)

if(TARGET zlink-cpp)
  target_link_libraries(zlink-codec-json INTERFACE zlink-cpp)
else()
  target_include_directories(zlink-codec-json INTERFACE
    ${ZLINK_CPP_DIR}/include
    ${ZLINK_CORE_DIR}/include
  )
endif()

target_include_directories(zlink-codec-json INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/include
)
target_link_libraries(zlink-codec-json INTERFACE nlohmann_json::nlohmann_json)
