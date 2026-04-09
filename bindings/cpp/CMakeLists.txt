cmake_minimum_required(VERSION 3.10)
project(zlink_cpp LANGUAGES CXX)

set(ZLINK_CORE_DIR "${CMAKE_SOURCE_DIR}/core" CACHE PATH "Path to zlink core")
set(ZLINK_CPP_STANDALONE FALSE)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(ZLINK_CPP_STANDALONE TRUE)
endif()

add_library(zlink-cpp INTERFACE)
target_include_directories(zlink-cpp INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/include
  ${ZLINK_CORE_DIR}/include
)

target_compile_features(zlink-cpp INTERFACE cxx_std_17)

find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
  add_custom_target(zlink_cpp_api_docs
    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/Doxyfile
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    COMMENT "Generating zlink C++ binding API reference"
    VERBATIM
  )
endif()

if(NOT DEFINED ZLINK_CPP_BUILD_TESTS)
  option(ZLINK_CPP_BUILD_TESTS "Build C++ binding tests" OFF)
endif()

if(NOT DEFINED ZLINK_CPP_BUILD_SAMPLES)
  option(ZLINK_CPP_BUILD_SAMPLES "Build C++ binding samples" OFF)
endif()

if(ZLINK_CPP_BUILD_TESTS OR ZLINK_CPP_BUILD_SAMPLES)
  if(NOT TARGET libzlink)
    message(FATAL_ERROR "libzlink target not found. Build core first or add_subdirectory(core)")
  endif()

  function(add_cpp_binding_executable target_name source_file)
    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PRIVATE libzlink zlink-cpp)
    if(MSVC)
      target_compile_options(${target_name} PRIVATE /UNDEBUG)
    else()
      target_compile_options(${target_name} PRIVATE -UNDEBUG)
    endif()
  endfunction()

  function(configure_cpp_binding_runtime target_name)
    set_tests_properties(${target_name} PROPERTIES
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    if(WIN32)
      set_tests_properties(${target_name} PROPERTIES
        ENVIRONMENT "PATH=$<TARGET_FILE_DIR:libzlink>;$ENV{PATH}")
    endif()
  endfunction()
endif()

if(ZLINK_CPP_BUILD_SAMPLES)
  function(add_cpp_sample target_name source_file)
    add_cpp_binding_executable(${target_name} ${source_file})
    if(UNIX AND NOT WIN32)
      add_test(NAME sample_smoke_${target_name}
        COMMAND /bin/bash -lc "$<TARGET_FILE:${target_name}>")
    else()
      add_test(NAME sample_smoke_${target_name} COMMAND ${target_name})
    endif()
    set_tests_properties(sample_smoke_${target_name} PROPERTIES
      LABELS "sample-smoke"
      TIMEOUT 120
    )
    configure_cpp_binding_runtime(sample_smoke_${target_name})
  endfunction()

  add_cpp_sample(
    sample_cpp_pair_recv_sample
    samples/pair_recv_sample.cpp
  )
  add_cpp_sample(
    sample_cpp_pair_callback_sample
    samples/pair_callback_sample.cpp
  )
  add_cpp_sample(
    sample_cpp_pubsub_recv_sample
    samples/pubsub_recv_sample.cpp
  )
  add_cpp_sample(
    sample_cpp_pubsub_callback_sample
    samples/pubsub_callback_sample.cpp
  )
  add_cpp_sample(
    sample_cpp_dealer_router_recv_sample
    samples/dealer_router_recv_sample.cpp
  )
  add_cpp_sample(
    sample_cpp_dealer_router_callback_sample
    samples/dealer_router_callback_sample.cpp
  )
  add_cpp_sample(
    sample_cpp_stream_recv_sample
    samples/stream_recv_sample.cpp
  )
  add_cpp_sample(
    sample_cpp_stream_callback_sample
    samples/stream_callback_sample.cpp
  )
  add_cpp_sample(
    sample_cpp_spot_recv_sample
    samples/spot_recv_sample.cpp
  )
  add_cpp_sample(
    sample_cpp_spot_callback_sample
    samples/spot_callback_sample.cpp
  )
  add_cpp_sample(
    sample_cpp_monitor_recv_sample
    samples/monitor_recv_sample.cpp
  )
endif()

if(ZLINK_CPP_BUILD_TESTS)
  function(add_cpp_contract_test target_name source_file)
    add_cpp_binding_executable(${target_name} ${source_file})
    if(UNIX AND NOT WIN32)
      add_test(NAME ${target_name}
        COMMAND /bin/bash -lc "$<TARGET_FILE:${target_name}>")
    else()
      add_test(NAME ${target_name} COMMAND ${target_name})
    endif()
    set_tests_properties(${target_name} PROPERTIES
      LABELS "contract"
      TIMEOUT 120
    )
    configure_cpp_binding_runtime(${target_name})
  endfunction()

  add_cpp_contract_test(
    test_cpp_contract_message
    tests/contract/test_cpp_contract_message.cpp
  )
  add_cpp_contract_test(
    test_cpp_contract_socket
    tests/contract/test_cpp_contract_socket.cpp
  )
  add_cpp_contract_test(
    test_cpp_contract_request_reply
    tests/contract/test_cpp_contract_request_reply.cpp
  )
  add_cpp_contract_test(
    test_cpp_contract_callback_mode
    tests/contract/test_cpp_contract_callback_mode.cpp
  )
  add_cpp_contract_test(
    test_cpp_contract_options
    tests/contract/test_cpp_contract_options.cpp
  )
  add_cpp_contract_test(
    test_cpp_contract_monitor
    tests/contract/test_cpp_contract_monitor.cpp
  )
  add_cpp_contract_test(
    test_cpp_contract_service
    tests/contract/test_cpp_contract_service.cpp
  )
  add_cpp_contract_test(
    test_cpp_contract_behavior
    tests/contract/test_cpp_contract_behavior.cpp
  )
endif()

option(ZLINK_CPP_BUILD_BENCHMARKS "Build C++ binding benchmarks" OFF)

if(ZLINK_CPP_BUILD_BENCHMARKS)
  if(NOT TARGET libzlink)
    message(FATAL_ERROR "libzlink target not found. Build core first or add_subdirectory(core)")
  endif()
  add_subdirectory(perf)
endif()
