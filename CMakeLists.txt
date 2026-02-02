cmake_minimum_required(VERSION 3.10)
project(zlink-root LANGUAGES C CXX)

if(BUILD_TESTS)
  enable_testing()
endif()

# Root wrapper to keep existing build scripts working.
add_subdirectory(core)
