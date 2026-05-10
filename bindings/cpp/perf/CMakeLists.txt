cmake_minimum_required(VERSION 3.10)
project(zlink_cpp_perf LANGUAGES CXX)

if(NOT DEFINED ZLINK_CPP_CORE_TARGET)
  message(FATAL_ERROR "ZLINK_CPP_CORE_TARGET is not configured")
endif()

find_package(Threads REQUIRED)

set(PERF_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(PERF_CPP_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../include")
set(PERF_CORE_INCLUDE_DIR "${ZLINK_CORE_DIR}/include")
set(PERF_ZLINK_C_DIR "${PERF_ROOT_DIR}/../../c")
set(PERF_ZLINK_C_INCLUDE_DIR "${PERF_ZLINK_C_DIR}/include")

function(resolve_perf_zlink_c_target out_target_var out_runtime_dir_var)
  if(TARGET zlink_c)
    set(${out_target_var} zlink_c PARENT_SCOPE)
    set(${out_runtime_dir_var} "$<TARGET_FILE_DIR:zlink_c>" PARENT_SCOPE)
    return()
  endif()

  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_libname "libzlink_c.so")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(_libname "libzlink_c.dylib")
  elseif(WIN32)
    set(_libname "zlink_c.dll")
  else()
    set(_libname "")
  endif()

  if(NOT _libname)
    message(FATAL_ERROR "Unsupported platform for zlink_c runtime resolution")
  endif()

  set(_candidate_dirs
    "${PERF_ZLINK_C_DIR}/build"
    "${PERF_ZLINK_C_DIR}/build/lib"
    "${PERF_ZLINK_C_DIR}/build/bin")
  find_file(_zlink_c_lib
    NAMES "${_libname}"
    PATHS ${_candidate_dirs}
    NO_DEFAULT_PATH)
  if(NOT _zlink_c_lib)
    message(FATAL_ERROR
      "No zlink_c runtime found. Build bindings/c first under "
      "${PERF_ZLINK_C_DIR}/build or include bindings/c in the active build.")
  endif()

  if(NOT TARGET zlink_c_perf_runtime)
    add_library(zlink_c_perf_runtime SHARED IMPORTED GLOBAL)
    set_target_properties(zlink_c_perf_runtime PROPERTIES
      IMPORTED_LOCATION "${_zlink_c_lib}"
    )
  endif()

  get_filename_component(_zlink_c_runtime_dir "${_zlink_c_lib}" DIRECTORY)
  set(${out_target_var} zlink_c_perf_runtime PARENT_SCOPE)
  set(${out_runtime_dir_var} "${_zlink_c_runtime_dir}" PARENT_SCOPE)
endfunction()

resolve_perf_zlink_c_target(PERF_ZLINK_C_TARGET PERF_ZLINK_C_RUNTIME_DIR)

set(PERF_SINGLE_COMMON_SRCS
  "${PERF_ROOT_DIR}/single/common/perf_single_common.cpp"
  "${PERF_ROOT_DIR}/single/common/perf_single_runner.cpp"
)

function(configure_perf_target target_name out_subdir)
  target_compile_features(${target_name} PRIVATE cxx_std_17)
  target_link_libraries(${target_name} PRIVATE
    ${ZLINK_CPP_CORE_TARGET}
    ${PERF_ZLINK_C_TARGET}
    zlink-cpp
    Threads::Threads)
  target_include_directories(${target_name} PRIVATE
    "${PERF_CORE_INCLUDE_DIR}"
    "${PERF_ZLINK_C_INCLUDE_DIR}"
    "${PERF_CPP_INCLUDE_DIR}"
    "${PERF_ROOT_DIR}/single/common"
    "${PERF_ROOT_DIR}/multi/common"
  )
  if(UNIX AND NOT APPLE)
    set_target_properties(${target_name} PROPERTIES
      BUILD_RPATH "${ZLINK_CPP_RUNTIME_DIR};${PERF_ZLINK_C_RUNTIME_DIR}"
    )
  elseif(APPLE)
    set_target_properties(${target_name} PROPERTIES
      BUILD_RPATH "${ZLINK_CPP_RUNTIME_DIR};${PERF_ZLINK_C_RUNTIME_DIR}"
    )
  endif()

  if(MSVC)
    target_compile_options(${target_name} PRIVATE /O2 /W4)
  else()
    target_compile_options(${target_name} PRIVATE -O3 -Wall -Wextra -Wpedantic)
  endif()

  set_target_properties(${target_name} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PERF_ROOT_DIR}/${out_subdir}/build"
  )
endfunction()

function(add_cpp_single_bench target_name src_path)
  add_executable(${target_name}
    ${PERF_SINGLE_COMMON_SRCS}
    "${src_path}"
  )
  configure_perf_target(${target_name} "single")
endfunction()

function(add_cpp_multi_server_bench target_name src_path)
  add_executable(${target_name}
    "${src_path}"
  )
  configure_perf_target(${target_name} "multi")
endfunction()

function(add_cpp_multi_client_bench target_name src_path)
  add_executable(${target_name}
    "${src_path}"
  )
  configure_perf_target(${target_name} "multi")
endfunction()

# Single suite
add_cpp_single_bench(cpp_perf_pair
  "${PERF_ROOT_DIR}/single/src/perf_pair.cpp")
add_cpp_single_bench(cpp_perf_pubsub
  "${PERF_ROOT_DIR}/single/src/perf_pubsub.cpp")
add_cpp_single_bench(cpp_perf_dealer_dealer
  "${PERF_ROOT_DIR}/single/src/perf_dealer_dealer.cpp")
add_cpp_single_bench(cpp_perf_dealer_router
  "${PERF_ROOT_DIR}/single/src/perf_dealer_router.cpp")
add_cpp_single_bench(cpp_perf_router_router
  "${PERF_ROOT_DIR}/single/src/perf_router_router.cpp")
add_cpp_single_bench(cpp_perf_spot
  "${PERF_ROOT_DIR}/single/src/perf_spot.cpp")

# Multi server suite (8 binaries)
add_cpp_multi_server_bench(cpp_comp_src_dealer_dealer_server
  "${PERF_ROOT_DIR}/multi/src/perf_dealer_dealer_server.cpp")
add_cpp_multi_server_bench(cpp_comp_src_dealer_router_server
  "${PERF_ROOT_DIR}/multi/src/perf_dealer_router_server.cpp")
add_cpp_multi_server_bench(cpp_comp_src_router_router_server
  "${PERF_ROOT_DIR}/multi/src/perf_router_router_server.cpp")
add_cpp_multi_server_bench(cpp_comp_src_pubsub_server
  "${PERF_ROOT_DIR}/multi/src/perf_pubsub_server.cpp")
add_cpp_multi_server_bench(cpp_comp_src_spot_server
  "${PERF_ROOT_DIR}/multi/src/perf_spot_server.cpp")
add_cpp_multi_server_bench(cpp_comp_src_spot_sendsend_server
  "${PERF_ROOT_DIR}/multi/src/perf_spot_sendsend_server.cpp")
add_cpp_multi_server_bench(cpp_comp_src_spot_reqrep_server
  "${PERF_ROOT_DIR}/multi/src/perf_spot_reqrep_server.cpp")
add_cpp_multi_server_bench(cpp_comp_src_stream_server
  "${PERF_ROOT_DIR}/multi/src/perf_stream_server.cpp")
configure_perf_target(cpp_comp_src_stream_server "multi")

# Multi client suite (6 binaries)
add_cpp_multi_client_bench(cpp_comp_src_dealer_dealer_client
  "${PERF_ROOT_DIR}/multi/src/perf_dealer_dealer_client.cpp")
add_cpp_multi_client_bench(cpp_comp_src_dealer_router_client
  "${PERF_ROOT_DIR}/multi/src/perf_dealer_router_client.cpp")
add_cpp_multi_client_bench(cpp_comp_src_router_router_client
  "${PERF_ROOT_DIR}/multi/src/perf_router_router_client.cpp")
add_cpp_multi_client_bench(cpp_comp_src_pubsub_client
  "${PERF_ROOT_DIR}/multi/src/perf_pubsub_client.cpp")
add_cpp_multi_client_bench(cpp_comp_src_spot_client
  "${PERF_ROOT_DIR}/multi/src/perf_spot_client.cpp")
add_cpp_multi_client_bench(cpp_comp_src_spot_sendsend_client
  "${PERF_ROOT_DIR}/multi/src/perf_spot_sendsend_client.cpp")
add_cpp_multi_client_bench(cpp_comp_src_spot_reqrep_client
  "${PERF_ROOT_DIR}/multi/src/perf_spot_reqrep_client.cpp")
