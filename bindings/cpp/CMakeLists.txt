cmake_minimum_required(VERSION 3.10)
project(zlink_cpp LANGUAGES CXX)

set(ZLINK_CORE_DIR "${CMAKE_SOURCE_DIR}/core" CACHE PATH "Path to zlink core")

add_library(zlink-cpp INTERFACE)
target_include_directories(zlink-cpp INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/include
  ${ZLINK_CORE_DIR}/include
)

target_compile_features(zlink-cpp INTERFACE cxx_std_11)

if(NOT DEFINED ZLINK_CPP_BUILD_TESTS)
  option(ZLINK_CPP_BUILD_TESTS "Build C++ binding tests" OFF)
endif()

if(ZLINK_CPP_BUILD_TESTS)
  if(NOT TARGET libzlink)
    message(FATAL_ERROR "libzlink target not found. Build core first or add_subdirectory(core)")
  endif()

  add_executable(test_cpp_basic tests/test_cpp_basic.cpp)
  target_link_libraries(test_cpp_basic PRIVATE libzlink zlink-cpp)
  add_test(NAME test_cpp_basic COMMAND test_cpp_basic)

  add_executable(test_cpp_spot tests/test_cpp_spot.cpp)
  target_link_libraries(test_cpp_spot PRIVATE libzlink zlink-cpp)
  add_test(NAME test_cpp_spot COMMAND test_cpp_spot)
endif()
