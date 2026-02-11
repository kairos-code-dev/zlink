cmake_minimum_required(VERSION 3.14)
project(zlink-mmorpg-sample LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Repository root ---
get_filename_component(ZLINK_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

# --- zlink library ---
# 1) Try vcpkg / system find_package first
find_package(zlink CONFIG QUIET)
if(TARGET libzlink)
  message(STATUS "Found zlink via find_package")
else()
  # 2) Fallback: use pre-built native library from the repository
  message(STATUS "find_package(zlink) not found — using native library fallback")

  # Detect platform
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
    set(ZLINK_NATIVE_DIR "${ZLINK_REPO_ROOT}/bindings/cpp/native/linux-x86_64")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
    set(ZLINK_NATIVE_DIR "${ZLINK_REPO_ROOT}/bindings/cpp/native/darwin-x86_64")
  elseif(WIN32)
    set(ZLINK_NATIVE_DIR "${ZLINK_REPO_ROOT}/bindings/cpp/native/windows-x86_64")
  else()
    message(FATAL_ERROR "No pre-built zlink native library for ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}")
  endif()

  # Find the shared library
  find_library(ZLINK_LIBRARY
    NAMES zlink
    PATHS "${ZLINK_NATIVE_DIR}"
    NO_DEFAULT_PATH)
  if(NOT ZLINK_LIBRARY)
    message(FATAL_ERROR "libzlink not found at ${ZLINK_NATIVE_DIR}")
  endif()

  # Create imported target
  add_library(libzlink SHARED IMPORTED)
  set_target_properties(libzlink PROPERTIES
    IMPORTED_LOCATION "${ZLINK_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${ZLINK_REPO_ROOT}/core/include")
  message(STATUS "Using native libzlink: ${ZLINK_LIBRARY}")
endif()

# --- zlink.hpp (C++ bindings header, not in vcpkg port yet) ---
set(ZLINK_CPP_INCLUDE_DIR "${ZLINK_REPO_ROOT}/bindings/cpp/include")
if(NOT EXISTS "${ZLINK_CPP_INCLUDE_DIR}/zlink.hpp")
  message(FATAL_ERROR "zlink.hpp not found at ${ZLINK_CPP_INCLUDE_DIR}")
endif()

# --- Asio (standalone) ---
find_package(asio CONFIG QUIET)
if(NOT TARGET asio::asio)
  find_path(ASIO_INCLUDE_DIR asio.hpp)
  if(NOT ASIO_INCLUDE_DIR)
    message(FATAL_ERROR "Standalone Asio not found. Install libasio-dev or use vcpkg.")
  endif()
  message(STATUS "Found standalone Asio at: ${ASIO_INCLUDE_DIR}")
endif()

# --- Common sources ---
set(COMMON_SOURCES
  common/app_protocol.cpp
  common/raw_framing.cpp
  common/ids.cpp)

# --- Workaround: ZLINK_NOEXCEPT is internal macro used in zlink.hpp ---
# Define it for external consumers until the binding header is fixed
add_compile_definitions(ZLINK_NOEXCEPT=noexcept)

# --- Workaround: Symbol collision between zlink.hpp wrapper classes and libzlink internals ---
# The C++ binding header (zlink.hpp) defines wrapper classes like zlink::discovery_t,
# zlink::gateway_t, etc. in the same namespace as the internal library classes.
# Their destructors share identical mangled names. Without hidden visibility, the
# dynamic linker may resolve internal library calls to the application's wrapper
# symbols, corrupting object state (check_tag failures).
if(NOT MSVC)
  add_compile_options(-fvisibility=hidden)
endif()

# --- sample-registry ---
add_executable(sample-registry registry/main.cpp)
target_link_libraries(sample-registry PRIVATE libzlink)
target_include_directories(sample-registry PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${ZLINK_CPP_INCLUDE_DIR})

# --- sample-front-server ---
add_executable(sample-front-server
  front_server/main.cpp
  front_server/front_server.cpp
  front_server/stream_ingress.cpp
  front_server/zone_service.cpp
  front_server/outgame_gateway.cpp
  front_server/spot_sync.cpp
  ${COMMON_SOURCES})
target_link_libraries(sample-front-server PRIVATE libzlink)
target_include_directories(sample-front-server PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${ZLINK_CPP_INCLUDE_DIR})

# --- sample-api-server ---
add_executable(sample-api-server
  api_server/main.cpp
  api_server/api_service.cpp
  ${COMMON_SOURCES})
target_link_libraries(sample-api-server PRIVATE libzlink)
target_include_directories(sample-api-server PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${ZLINK_CPP_INCLUDE_DIR})

# --- sample-raw-client (Asio TCP, no libzlink) ---
add_executable(sample-raw-client
  raw_client/main.cpp
  raw_client/asio_client.cpp
  raw_client/scenario_runner.cpp
  raw_client/console_display.cpp
  ${COMMON_SOURCES})
target_include_directories(sample-raw-client PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(sample-raw-client PRIVATE ASIO_STANDALONE)
if(TARGET asio::asio)
  target_link_libraries(sample-raw-client PRIVATE asio::asio)
else()
  target_include_directories(sample-raw-client PRIVATE ${ASIO_INCLUDE_DIR})
endif()
if(UNIX)
  target_link_libraries(sample-raw-client PRIVATE pthread)
endif()
