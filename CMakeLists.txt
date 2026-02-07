cmake_minimum_required(VERSION 3.10)
project(zlink-root LANGUAGES C CXX)

option(ZLINK_CPP_BUILD_TESTS "Build C++ binding tests" OFF)
if(BUILD_TESTS OR ZLINK_CPP_BUILD_TESTS OR ZLINK_BUILD_TESTS)
  enable_testing()
endif()

# Root wrapper to keep existing build scripts working.
add_subdirectory(core)

option(ZLINK_BUILD_CPP_BINDINGS "Build C++ bindings" OFF)
if(ZLINK_BUILD_CPP_BINDINGS)
  add_subdirectory(bindings/cpp)
endif()
