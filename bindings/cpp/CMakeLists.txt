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
option(ZLINK_CPP_BUILD_EXAMPLES "Build C++ binding examples" OFF)

if(ZLINK_CPP_BUILD_TESTS)
  if(NOT TARGET libzlink)
    message(FATAL_ERROR "libzlink target not found. Build core first or add_subdirectory(core)")
  endif()

  function(add_cpp_binding_test test_name source_file)
    add_executable(${test_name} ${source_file})
    target_link_libraries(${test_name} PRIVATE libzlink zlink-cpp)
    add_test(NAME ${test_name} COMMAND ${test_name})
    if(WIN32)
      set_tests_properties(${test_name}
        PROPERTIES
          ENVIRONMENT "PATH=$<TARGET_FILE_DIR:libzlink>;$ENV{PATH}")
    endif()
  endfunction()

  add_cpp_binding_test(test_cpp_pair tests/test_cpp_pair.cpp)
  add_cpp_binding_test(test_cpp_pubsub tests/test_cpp_pubsub.cpp)
  add_cpp_binding_test(test_cpp_dealer_router tests/test_cpp_dealer_router.cpp)
  add_cpp_binding_test(test_cpp_xpub_xsub tests/test_cpp_xpub_xsub.cpp)
  add_cpp_binding_test(test_cpp_multipart tests/test_cpp_multipart.cpp)
  add_cpp_binding_test(test_cpp_discovery tests/test_cpp_discovery_gateway_spot.cpp)
endif()

if(ZLINK_CPP_BUILD_EXAMPLES)
  if(NOT TARGET libzlink)
    message(FATAL_ERROR "libzlink target not found. Build core first or add_subdirectory(core)")
  endif()

  add_executable(cpp_pair_basic examples/pair_basic.cpp)
  target_link_libraries(cpp_pair_basic PRIVATE libzlink zlink-cpp)

  add_executable(cpp_dealer_router examples/dealer_router.cpp)
  target_link_libraries(cpp_dealer_router PRIVATE libzlink zlink-cpp)

  add_executable(cpp_spot_basic examples/spot_basic.cpp)
  target_link_libraries(cpp_spot_basic PRIVATE libzlink zlink-cpp)

  add_executable(cpp_monitor_basic examples/monitor_basic.cpp)
  target_link_libraries(cpp_monitor_basic PRIVATE libzlink zlink-cpp)
endif()
