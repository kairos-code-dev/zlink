cmake_minimum_required(VERSION 3.10)
project(zlink_cpp LANGUAGES CXX)

get_filename_component(ZLINK_CPP_BINDINGS_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
get_filename_component(ZLINK_REPO_ROOT_DIR "${ZLINK_CPP_BINDINGS_DIR}/../.." ABSOLUTE)

set(ZLINK_CORE_DIR "${ZLINK_REPO_ROOT_DIR}/core" CACHE PATH "Path to zlink core")
set(ZLINK_CPP_CORE_BUILD_DIR "${ZLINK_REPO_ROOT_DIR}/core/build" CACHE PATH
    "Path to an existing zlink core build directory")
set(ZLINK_CPP_STANDALONE FALSE)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(ZLINK_CPP_STANDALONE TRUE)
endif()

function(zlink_cpp_resolve_native_layout out_dir_var out_lib_var)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
      set(_subdir "linux-aarch64")
    else()
      set(_subdir "linux-x86_64")
    endif()
    set(_libname "libzlink.so")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
      set(_subdir "darwin-aarch64")
    else()
      set(_subdir "darwin-x86_64")
    endif()
    set(_libname "libzlink.dylib")
  elseif(WIN32)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|aarch64|arm64)$")
      set(_subdir "windows-aarch64")
    else()
      set(_subdir "windows-x86_64")
    endif()
    set(_libname "zlink.dll")
  else()
    set(_subdir "")
    set(_libname "")
  endif()

  if(_subdir)
    set(${out_dir_var} "${CMAKE_CURRENT_LIST_DIR}/native/${_subdir}" PARENT_SCOPE)
    set(${out_lib_var} "${CMAKE_CURRENT_LIST_DIR}/native/${_subdir}/${_libname}" PARENT_SCOPE)
  else()
    set(${out_dir_var} "" PARENT_SCOPE)
    set(${out_lib_var} "" PARENT_SCOPE)
  endif()
endfunction()

function(zlink_cpp_resolve_core_build_layout out_dir_var out_lib_var)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_libname "libzlink.so")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(_libname "libzlink.dylib")
  elseif(WIN32)
    set(_libname "zlink.dll")
  else()
    set(_libname "")
  endif()

  if(NOT _libname)
    set(${out_dir_var} "" PARENT_SCOPE)
    set(${out_lib_var} "" PARENT_SCOPE)
    return()
  endif()

  set(_candidate_dirs
    "${ZLINK_CPP_CORE_BUILD_DIR}/lib"
    "${ZLINK_CPP_CORE_BUILD_DIR}/bin")
  find_file(_core_lib
    NAMES "${_libname}"
    PATHS ${_candidate_dirs}
    NO_DEFAULT_PATH)
  if(_core_lib)
    get_filename_component(_core_dir "${_core_lib}" DIRECTORY)
    set(${out_dir_var} "${_core_dir}" PARENT_SCOPE)
    set(${out_lib_var} "${_core_lib}" PARENT_SCOPE)
    return()
  endif()

  set(${out_dir_var} "" PARENT_SCOPE)
  set(${out_lib_var} "" PARENT_SCOPE)
endfunction()

function(zlink_cpp_ensure_core_target out_target_var out_runtime_dir_var)
  if(TARGET libzlink)
    set(${out_target_var} libzlink PARENT_SCOPE)
    set(${out_runtime_dir_var} "$<TARGET_FILE_DIR:libzlink>" PARENT_SCOPE)
    return()
  endif()

  zlink_cpp_resolve_core_build_layout(_core_build_dir _core_build_lib)
  if(_core_build_lib)
    if(NOT TARGET zlink-core-build-runtime)
      add_library(zlink-core-build-runtime SHARED IMPORTED GLOBAL)
      set_target_properties(zlink-core-build-runtime PROPERTIES
        IMPORTED_LOCATION "${_core_build_lib}"
      )
    endif()
    set(${out_target_var} zlink-core-build-runtime PARENT_SCOPE)
    set(${out_runtime_dir_var} "${_core_build_dir}" PARENT_SCOPE)
    return()
  endif()

  zlink_cpp_resolve_native_layout(_native_dir _native_lib)

  if(_native_lib AND EXISTS "${_native_lib}")
    if(NOT TARGET zlink-native)
      add_library(zlink-native SHARED IMPORTED GLOBAL)
      set_target_properties(zlink-native PROPERTIES
        IMPORTED_LOCATION "${_native_lib}"
      )
    endif()
    set(${out_target_var} zlink-native PARENT_SCOPE)
    set(${out_runtime_dir_var} "${_native_dir}" PARENT_SCOPE)
    return()
  endif()

  message(FATAL_ERROR
    "No zlink core runtime found for this platform. "
    "Expected an existing libzlink target, a core build under "
    "${ZLINK_CPP_CORE_BUILD_DIR}, or ${_native_lib}.")
endfunction()

zlink_cpp_ensure_core_target(ZLINK_CPP_CORE_TARGET ZLINK_CPP_RUNTIME_DIR)

add_library(zlink-cpp INTERFACE)
target_include_directories(zlink-cpp INTERFACE
  ${ZLINK_CORE_DIR}/include
  ${ZLINK_CORE_DIR}/external/boost
  ${CMAKE_CURRENT_LIST_DIR}/include
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

if(NOT DEFINED ZLINK_CPP_REGISTER_CTEST)
  option(
    ZLINK_CPP_REGISTER_CTEST
    "Register C++ binding tests and samples in the active CTest project"
    ${ZLINK_CPP_STANDALONE}
  )
endif()

if(ZLINK_CPP_STANDALONE AND ZLINK_CPP_REGISTER_CTEST)
  enable_testing()
endif()

if(ZLINK_CPP_BUILD_TESTS OR ZLINK_CPP_BUILD_SAMPLES)
  function(add_cpp_binding_executable target_name source_file)
    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PRIVATE ${ZLINK_CPP_CORE_TARGET} zlink-cpp)
    if(UNIX AND NOT APPLE)
      set_target_properties(${target_name} PROPERTIES
        BUILD_RPATH "${ZLINK_CPP_RUNTIME_DIR}"
      )
    elseif(APPLE)
      set_target_properties(${target_name} PROPERTIES
        BUILD_RPATH "${ZLINK_CPP_RUNTIME_DIR}"
      )
    endif()
    if(MSVC)
      target_compile_options(${target_name} PRIVATE /UNDEBUG)
    else()
      target_compile_options(${target_name} PRIVATE -UNDEBUG)
    endif()
  endfunction()

  function(configure_cpp_binding_runtime target_name)
    set_tests_properties(${target_name} PROPERTIES
      WORKING_DIRECTORY ${ZLINK_REPO_ROOT_DIR})
    if(WIN32)
      set_tests_properties(${target_name} PROPERTIES
        ENVIRONMENT "PATH=${ZLINK_CPP_RUNTIME_DIR};$ENV{PATH}")
    elseif(APPLE)
      set_tests_properties(${target_name} PROPERTIES
        ENVIRONMENT "DYLD_LIBRARY_PATH=${ZLINK_CPP_RUNTIME_DIR}:$ENV{DYLD_LIBRARY_PATH}")
    elseif(UNIX)
      set_tests_properties(${target_name} PROPERTIES
        ENVIRONMENT "LD_LIBRARY_PATH=${ZLINK_CPP_RUNTIME_DIR}:$ENV{LD_LIBRARY_PATH}")
    endif()
  endfunction()
endif()

if(ZLINK_CPP_BUILD_SAMPLES)
  function(add_cpp_sample target_name source_file)
    add_cpp_binding_executable(${target_name} ${source_file})
    if(ZLINK_CPP_REGISTER_CTEST)
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
    endif()
  endfunction()

  add_cpp_sample(
    sample_cpp_request_reply_async_sample
    samples/request_reply_async_sample.cpp
  )
  target_compile_features(sample_cpp_request_reply_async_sample PRIVATE cxx_std_20)
  add_cpp_sample(
    sample_cpp_pair_recv_sample
    samples/pair_recv_sample.cpp
  )
  add_cpp_sample(
    sample_cpp_pubsub_recv_sample
    samples/pubsub_recv_sample.cpp
  )
  add_cpp_sample(
    sample_cpp_dealer_router_recv_sample
    samples/dealer_router_recv_sample.cpp
  )
  add_cpp_sample(
    sample_cpp_stream_recv_sample
    samples/stream_recv_sample.cpp
  )
  add_cpp_sample(
    sample_cpp_stream_packet_callback_sample
    samples/stream_packet_callback_sample.cpp
  )
  add_cpp_sample(
    sample_cpp_spot_recv_sample
    samples/spot_recv_sample.cpp
  )
  add_cpp_sample(
    sample_cpp_spot_request_async_sample
    samples/spot_request_async_sample.cpp
  )
  add_cpp_sample(
    sample_cpp_monitor_recv_sample
    samples/monitor_recv_sample.cpp
  )
  add_cpp_sample(
    sample_cpp_discovery_registry_sample
    samples/discovery_registry_sample.cpp
  )
  add_cpp_sample(
    sample_cpp_registry_query_sample
    samples/registry_query_sample.cpp
  )
  add_cpp_sample(
    sample_cpp_actor_room_server_sample
    samples/actor_room_server_sample.cpp
  )
  add_cpp_sample(
    sample_cpp_actor_gateway_relay_sample
    samples/actor_gateway_relay_sample.cpp
  )
  add_cpp_sample(
    sample_cpp_actor_single_player_queue_sample
    samples/actor_single_player_queue_sample.cpp
  )
endif()

if(ZLINK_CPP_BUILD_TESTS)
  function(add_cpp_contract_test target_name source_file)
    add_cpp_binding_executable(${target_name} ${source_file})
    if(ZLINK_CPP_REGISTER_CTEST)
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
    endif()
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
    test_cpp_contract_actor_header
    tests/contract/test_cpp_contract_actor_header.cpp
  )
  add_cpp_contract_test(
    test_cpp_contract_behavior
    tests/contract/test_cpp_contract_behavior.cpp
  )
endif()

option(ZLINK_CPP_BUILD_BENCHMARKS "Build C++ binding benchmarks" OFF)

if(ZLINK_CPP_BUILD_BENCHMARKS)
  add_subdirectory(perf)
endif()
