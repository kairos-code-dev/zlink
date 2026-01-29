# CMake build script for Zlink (zlink distribution)

# Set minimum CMake version before project()
cmake_minimum_required(VERSION 3.10)

# Enable VERSION in project() command
if(POLICY CMP0048)
  cmake_policy(SET CMP0048 NEW)
endif()

project(zlink VERSION 0.1.4 LANGUAGES C CXX)

include(CheckIncludeFiles)
include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
include(CheckLibraryExists)
include(CheckCSourceCompiles)
include(CheckCSourceRuns)
include(CMakeDependentOption)
include(CheckCXXSymbolExists)
include(CheckStructHasMember)
include(CheckTypeSize)
include(FindThreads)
include(GNUInstallDirs)
include(CheckTypeSize)
include(CMakePackageConfigHelpers)

list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_CURRENT_SOURCE_DIR}")
set(ZLINK_CMAKE_MODULES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/builds/cmake/Modules)
list(APPEND CMAKE_MODULE_PATH ${ZLINK_CMAKE_MODULES_DIR})

include(TestZLINKVersion)
include(ZLINKSourceRunChecks)
include(ZLINKSupportMacros)

find_package(PkgConfig)

# Set lists to empty beforehand as to not accidentally take values from parent
set(sources)
set(cxx-sources)
set(html-docs)
set(target_outputs)

option(ENABLE_ASAN "Build with address sanitizer" OFF)
if(ENABLE_ASAN)
  message(STATUS "Instrumenting with Address Sanitizer")
  set(CMAKE_BUILD_TYPE "RelWithDebInfo")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fsanitize-address-use-after-scope -fno-omit-frame-pointer")
  set(CMAKE_CXX_FLAGS
      "${CMAKE_CXX_FLAGS} -fsanitize=address -fsanitize-address-use-after-scope -fno-omit-frame-pointer")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address -fsanitize-address-use-after-scope")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address -fsanitize-address-use-after-scope")
endif()

# NOTE: Running libzlink under TSAN doesn't make much sense -- synchronization in libzlink is to some extent
# handled by the code "knowing" what threads are allowed to do, rather than by enforcing those
# restrictions, so TSAN generates a lot of (presumably) false positives from libzlink.
# The settings below are intended to enable libzlink to be built with minimal support for TSAN
# such that it can be used along with other code that is also built with TSAN.
option(ENABLE_TSAN "Build with thread sanitizer" OFF)
if(ENABLE_TSAN)
  message(STATUS "Instrumenting with Thread Sanitizer")
  set(CMAKE_BUILD_TYPE "RelWithDebInfo")
  set(TSAN_FLAGS "-fno-omit-frame-pointer -fsanitize=thread")
  set(TSAN_CCFLAGS "${TSAN_CCFLAGS} -mllvm -tsan-instrument-memory-accesses=0")
  set(TSAN_CCFLAGS "${TSAN_CCFLAGS} -mllvm -tsan-instrument-atomics=0")
  set(TSAN_CCFLAGS "${TSAN_CCFLAGS} -mllvm -tsan-instrument-func-entry-exit=1")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${TSAN_FLAGS} ${TSAN_CCFLAGS} -fPIE")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TSAN_FLAGS} ${TSAN_CCFLAGS} -fPIE")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${TSAN_FLAGS} -pie -Qunused-arguments")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${TSAN_FLAGS} -pie -Qunused-arguments")
endif()

option(ENABLE_UBSAN "Build with undefined behavior sanitizer" OFF)
if(ENABLE_UBSAN)
  message(STATUS "Instrumenting with Undefined Behavior Sanitizer")
  set(CMAKE_BUILD_TYPE "Debug")
  set(UBSAN_FLAGS "${UBSAN_FLAGS} -fno-omit-frame-pointer")
  set(UBSAN_FLAGS "${UBSAN_FLAGS} -fsanitize=undefined")
  set(UBSAN_FLAGS "${UBSAN_FLAGS} -fsanitize=implicit-conversion")
  set(UBSAN_FLAGS "${UBSAN_FLAGS} -fsanitize=implicit-integer-truncation")
  set(UBSAN_FLAGS "${UBSAN_FLAGS} -fsanitize=integer")
  set(UBSAN_FLAGS "${UBSAN_FLAGS} -fsanitize=nullability")
  set(UBSAN_FLAGS "${UBSAN_FLAGS} -fsanitize=vptr")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${UBSAN_FLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${UBSAN_FLAGS}")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${UBSAN_FLAGS}")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${UBSAN_FLAGS}")
endif()

option(ENABLE_INTRINSICS "Build using compiler intrinsics for atomic ops" OFF)
if(ENABLE_INTRINSICS)
  message(STATUS "Using compiler intrinsics for atomic ops")
  add_definitions(-DZLINK_HAVE_ATOMIC_INTRINSICS)
endif()

option(ENABLE_NATIVE_OPTIMIZATIONS "Enable -march=native for release builds" OFF)
if(ENABLE_NATIVE_OPTIMIZATIONS AND NOT MSVC)
  set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -march=native")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=native")
endif()

option(ENABLE_NATIVE_TUNE "Enable -mtune=native for release builds" OFF)
if(ENABLE_NATIVE_TUNE AND NOT MSVC)
  set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -mtune=native")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -mtune=native")
endif()

# Release build optimizations for production deployment
option(ENABLE_LTO "Enable Link-Time Optimization (LTO/IPO)" ON)

if(ENABLE_LTO AND NOT CMAKE_BUILD_TYPE MATCHES "Debug")
    message(STATUS "Enabling Link-Time Optimization (LTO/IPO)")

    # Use CMake's native LTO support (CMake 3.9+)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_output)

    if(ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(WARNING "LTO not supported by this compiler: ${ipo_output}")
    endif()
endif()

set(ZLINK_OUTPUT_BASENAME
    "zlink"
    CACHE STRING "Output zlink library base name")

if(${CMAKE_SYSTEM_NAME} STREQUAL Darwin)
  # Find more information: https://cmake.org/Wiki/CMake_RPATH_handling

  # Apply CMP0042: MACOSX_RPATH is enabled by default
  cmake_policy(SET CMP0042 NEW)

  # Add an install rpath if it is not a system directory
  list(FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}" isSystemDir)
  if("${isSystemDir}" STREQUAL "-1")
    set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
  endif()

  # Add linker search paths pointing to external dependencies
  set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
endif()

if (NOT MSVC)
  if(NOT CMAKE_CXX_FLAGS MATCHES "-std=" AND NOT CXX_STANDARD AND NOT CMAKE_CXX_STANDARD)
    # Allow C++ standard override via ZLINK_CXX_STANDARD option
    set(ZLINK_CXX_STANDARD "17" CACHE STRING "C++ standard to use (11, 14, 17, 20, etc.).")

    if(ZLINK_CXX_STANDARD)
      check_cxx_compiler_flag("-std=c++${ZLINK_CXX_STANDARD}" COMPILER_SUPPORTS_CXX${ZLINK_CXX_STANDARD})
      if(COMPILER_SUPPORTS_CXX${ZLINK_CXX_STANDARD})
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++${ZLINK_CXX_STANDARD}")
        message(STATUS "Using C++${ZLINK_CXX_STANDARD} standard")
      else()
        message(WARNING "Compiler does not support C++${ZLINK_CXX_STANDARD}, falling back to C++11")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
      endif()
    endif()
  endif()
  if(NOT CMAKE_C_FLAGS MATCHES "-std=" AND NOT C_STANDARD AND NOT CMAKE_C_STANDARD)
    check_c_compiler_flag("-std=c11" COMPILER_SUPPORTS_C11)
    if(COMPILER_SUPPORTS_C11)
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11")
    else()
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99")
    endif()
  endif()

  # clang 6 has a warning that does not make sense on multi-platform code
  check_cxx_compiler_flag("-Wno-tautological-constant-compare" CXX_HAS_TAUT_WARNING)
  if(CXX_HAS_TAUT_WARNING)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-tautological-constant-compare")
  endif()
  check_c_compiler_flag("-Wno-tautological-constant-compare" CC_HAS_TAUT_WARNING)
  if(CC_HAS_TAUT_WARNING)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-tautological-constant-compare")
  endif()
endif()

# Will be used to add flags to pkg-config useful when apps want to statically link
set(pkg_config_libs_private "")
set(pkg_config_names_private "")
set(pkg_config_defines "")

# ASIO backend is mandatory in zlink - no option needed
# WebSocket is always enabled - no option needed
option(WITH_TLS "Build with TLS support (requires OpenSSL)" ON)
option(WITH_OPENPGM "Build with OpenPGM support (PGM/EPGM transports)" OFF)

# ASIO backend is mandatory in zlink
set(ZLINK_BOOST_INCLUDE_DIR "" CACHE PATH "Boost include directory (override bundled Boost)")
if(ZLINK_BOOST_INCLUDE_DIR)
  set(BUNDLED_BOOST_INCLUDE_DIR "${ZLINK_BOOST_INCLUDE_DIR}")
else()
  set(BUNDLED_BOOST_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/boost")
endif()
if(EXISTS "${BUNDLED_BOOST_INCLUDE_DIR}/boost/asio.hpp")
  message(STATUS "ASIO headers found: ${BUNDLED_BOOST_INCLUDE_DIR}")
  set(ZLINK_HAVE_ASIO 1)
  # Force ASIO as the I/O poller backend
  set(POLLER "asio" CACHE STRING "I/O thread polling system (forced to asio)" FORCE)
  message(STATUS "Using ASIO as mandatory I/O backend")
else()
  message(FATAL_ERROR "ASIO headers not found at ${BUNDLED_BOOST_INCLUDE_DIR} - ASIO is mandatory")
endif()

# TLS support using OpenSSL (enabled by default)
if(WITH_TLS)
  find_package(OpenSSL REQUIRED)
  if(OpenSSL_FOUND)
    message(STATUS "OpenSSL found: ${OPENSSL_VERSION}")
    # Internal flag kept as ZLINK_HAVE_ASIO_SSL for compatibility
    set(ZLINK_HAVE_ASIO_SSL 1)
  else()
    message(FATAL_ERROR "OpenSSL not found. Install libssl-dev (required for WITH_TLS)")
  endif()
endif()

# WebSocket support using Boost.Beast (always enabled in zlink)
set(BUNDLED_BEAST_HEADER "${BUNDLED_BOOST_INCLUDE_DIR}/boost/beast.hpp")
if(EXISTS "${BUNDLED_BEAST_HEADER}")
  message(STATUS "Beast headers found - WebSocket support enabled")
  # Internal flags kept for compatibility
  set(ZLINK_HAVE_ASIO_WS 1)
  # WSS (WebSocket Secure) requires TLS
  if(ZLINK_HAVE_ASIO_SSL)
    set(ZLINK_HAVE_ASIO_WSS 1)
    message(STATUS "WSS (Secure WebSocket) enabled with TLS")
  endif()
else()
  message(FATAL_ERROR "Beast headers not found at ${BUNDLED_BEAST_HEADER} - WebSocket is mandatory")
endif()

if(APPLE)
  option(ZLINK_BUILD_FRAMEWORK "Build as OS X framework" OFF)
endif()

# Set feature flags for transport layer
if(ZLINK_HAVE_ASIO_SSL)
  set(ZLINK_HAVE_TLS 1)
  message(STATUS "TLS transport enabled")
endif()

# WebSocket is always enabled in zlink
if(ZLINK_HAVE_ASIO_WS)
  set(ZLINK_HAVE_WS 1)
  message(STATUS "WebSocket transport enabled")
  if(ZLINK_HAVE_ASIO_WSS)
    set(ZLINK_HAVE_WSS 1)
    message(STATUS "Secure WebSocket transport enabled")
  endif()
endif()

# OpenPGM (optional, required for pgm/epgm transports)
if(WITH_OPENPGM)
  if(NOT PKG_CONFIG_FOUND)
    message(FATAL_ERROR "PkgConfig is required to locate OpenPGM")
  endif()
  if(NOT OPENPGM_PKGCONFIG_NAME)
    set(_openpgm_candidates "openpgm-5.3" "openpgm-5.2")
    foreach(_openpgm_name IN LISTS _openpgm_candidates)
      pkg_check_modules(OPENPGM QUIET ${_openpgm_name})
      if(OPENPGM_FOUND)
        set(OPENPGM_PKGCONFIG_NAME ${_openpgm_name})
        break()
      endif()
    endforeach()
  endif()
  if(NOT OPENPGM_FOUND)
    if(NOT OPENPGM_PKGCONFIG_NAME)
      set(OPENPGM_PKGCONFIG_NAME "openpgm-5.3")
    endif()
    pkg_check_modules(OPENPGM ${OPENPGM_PKGCONFIG_NAME})
  endif()
  if(OPENPGM_PKGCONFIG_NAME)
    set(OPENPGM_PKGCONFIG_NAME
        ${OPENPGM_PKGCONFIG_NAME}
        CACHE STRING "Name pkg-config shall use to find openpgm libraries and include paths"
        FORCE)
  endif()
  if(OPENPGM_FOUND)
    message(STATUS "${OPENPGM_PKGCONFIG_NAME} found")
    set(ZLINK_HAVE_OPENPGM 1)
    include_directories(${OPENPGM_INCLUDE_DIRS})
    link_directories(${OPENPGM_LIBRARY_DIRS})
    set(pkg_config_names_private "${pkg_config_names_private} ${OPENPGM_PKGCONFIG_NAME}")
  else()
    message(FATAL_ERROR
      "${OPENPGM_PKGCONFIG_NAME} not found. openpgm is searched via "
      "`pkg-config ${OPENPGM_PKGCONFIG_NAME}`. Set OPENPGM_PKGCONFIG_NAME "
      "or disable WITH_OPENPGM.")
  endif()
endif()


# zlink: WebSocket block disabled (files removed)
# if(ENABLE_WS)
#   list(
#     APPEND
#     sources
#     ${CMAKE_CURRENT_SOURCE_DIR}/src/ws_address.cpp
#     ${CMAKE_CURRENT_SOURCE_DIR}/src/ws_connecter.cpp
#     ${CMAKE_CURRENT_SOURCE_DIR}/src/ws_decoder.cpp
#     ${CMAKE_CURRENT_SOURCE_DIR}/src/ws_encoder.cpp
#     ${CMAKE_CURRENT_SOURCE_DIR}/src/ws_engine.cpp
#     ${CMAKE_CURRENT_SOURCE_DIR}/src/ws_listener.cpp
#     ${CMAKE_CURRENT_SOURCE_DIR}/src/ws_address.hpp
#     ${CMAKE_CURRENT_SOURCE_DIR}/src/ws_connecter.hpp
#     ${CMAKE_CURRENT_SOURCE_DIR}/src/ws_decoder.hpp
#     ${CMAKE_CURRENT_SOURCE_DIR}/src/ws_encoder.hpp
#     ${CMAKE_CURRENT_SOURCE_DIR}/src/ws_engine.hpp
#     ${CMAKE_CURRENT_SOURCE_DIR}/src/ws_listener.hpp
#     ${CMAKE_CURRENT_SOURCE_DIR}/src/ws_protocol.hpp)
#   set(ZLINK_HAVE_WS 1)
# 
#   message(STATUS "Enable WebSocket transport")
# 
#   option(WITH_TLS "Use TLS for WSS support" ON)
#   option(WITH_NSS "Use NSS instead of builtin sha1" OFF)
# 
#   if(WITH_TLS)
#     find_package("GnuTLS" 3.6.7)
#     if(GNUTLS_FOUND)
#       set(pkg_config_names_private "${pkg_config_names_private} gnutls")
#       list(APPEND sources ${CMAKE_CURRENT_SOURCE_DIR}/src/wss_address.hpp
#            ${CMAKE_CURRENT_SOURCE_DIR}/src/wss_address.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/wss_engine.hpp
#            ${CMAKE_CURRENT_SOURCE_DIR}/src/wss_engine.cpp)
# 
#       message(STATUS "Enable WSS transport")
#       set(ZLINK_USE_GNUTLS 1)
#       set(ZLINK_HAVE_WSS 1)
#     else()
#       message(WARNING "No WSS support, you may want to install GnuTLS and run cmake again")
#     endif()
#   endif()
# endif()

# zlink: GNUTLS/NSS paths removed; ASIO SSL is handled via OpenSSL.

if(NOT MSVC)
  option(WITH_LIBBSD "Use libbsd instead of builtin strlcpy" ON)
  if(WITH_LIBBSD)
    pkg_check_modules(LIBBSD "libbsd")
    if(LIBBSD_FOUND)
      message(STATUS "Using libbsd")
      set(pkg_config_names_private "${pkg_config_names_private} libbsd")
      set(ZLINK_HAVE_LIBBSD 1)
    endif()
  endif()
  check_cxx_symbol_exists(strlcpy string.h ZLINK_HAVE_STRLCPY)
endif()

# allocator options (std::malloc only)

# zlink: CURVE security permanently disabled (libsodium removed)

set(SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")


option(WITH_MILITANT "Enable militant assertions" OFF)
if(WITH_MILITANT)
  add_definitions(-DZLINK_ACT_MILITANT)
endif()

set(API_POLLER
    ""
    CACHE STRING "Choose polling system for zlink_poll(er)_*. valid values are
  poll or select [default=poll unless POLLER=select]")

# POLLER is always 'asio' - no other options are supported

if(WIN32)
  if(CMAKE_SYSTEM_NAME STREQUAL "WindowsStore" AND CMAKE_SYSTEM_VERSION MATCHES "^10.0")
    set(ZLINK_HAVE_WINDOWS_UWP ON)
    set(ZLINK_HAVE_IPC OFF)
    # to remove compile warninging "D9002 ignoring unknown option"
    string(REPLACE "/Zi" "" CMAKE_CXX_FLAGS_DEBUG ${CMAKE_CXX_FLAGS_DEBUG})
    set(CMAKE_CXX_FLAGS_DEBUG
        ${CMAKE_CXX_FLAGS_DEBUG}
        CACHE STRING "" FORCE)
    string(REPLACE "/Zi" "" CMAKE_CXX_FLAGS_RELWITHDEBINFO ${CMAKE_CXX_FLAGS_RELWITHDEBINFO})
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO
        ${CMAKE_CXX_FLAGS_RELWITHDEBINFO}
        CACHE STRING "" FORCE)
    string(REPLACE "/Zi" "" CMAKE_CXX_FLAGS_DEBUG ${CMAKE_CXX_FLAGS_DEBUG})
  endif()
  # from https://stackoverflow.com/a/40217291/2019765
  macro(get_WIN32_WINNT version)
    if(CMAKE_SYSTEM_VERSION)
      set(ver ${CMAKE_SYSTEM_VERSION})
      string(REGEX MATCH "^([0-9]+).([0-9])" ver ${ver})
      string(REGEX MATCH "^([0-9]+)" verMajor ${ver})
      # Check for Windows 10, b/c we'll need to convert to hex 'A'.
      if("${verMajor}" MATCHES "10")
        set(verMajor "A")
        string(REGEX REPLACE "^([0-9]+)" ${verMajor} ver ${ver})
      endif("${verMajor}" MATCHES "10")
      # Remove all remaining '.' characters.
      string(REPLACE "." "" ver ${ver})
      # Prepend each digit with a zero.
      string(REGEX REPLACE "([0-9A-Z])" "0\\1" ver ${ver})
      set(${version} "0x${ver}")
    endif(CMAKE_SYSTEM_VERSION)
  endmacro(get_WIN32_WINNT)

  get_win32_winnt(ZLINK_WIN32_WINNT_DEFAULT)
  message(STATUS "Detected _WIN32_WINNT from CMAKE_SYSTEM_VERSION: ${ZLINK_WIN32_WINNT_DEFAULT}")

  # TODO limit _WIN32_WINNT to the actual Windows SDK version, which might be different from the default version
  # installed with Visual Studio
  if(MSVC_VERSION STREQUAL "1500" AND CMAKE_SYSTEM_VERSION VERSION_GREATER "6.0")
    set(ZLINK_WIN32_WINNT_LIMIT "0x0600")
  elseif(MSVC_VERSION STREQUAL "1600" AND CMAKE_SYSTEM_VERSION VERSION_GREATER "6.1")
    set(ZLINK_WIN32_WINNT_LIMIT "0x0601")
  elseif(MSVC_VERSION STREQUAL "1700" AND CMAKE_SYSTEM_VERSION VERSION_GREATER "6.1")
    set(ZLINK_WIN32_WINNT_LIMIT "0x0601")
  elseif(MSVC_VERSION STREQUAL "1800" AND CMAKE_SYSTEM_VERSION VERSION_GREATER "6.2")
    set(ZLINK_WIN32_WINNT_LIMIT "0x0602")
  endif()
  if(ZLINK_WIN32_WINNT_LIMIT)
    message(
      STATUS
        "Mismatch of Visual Studio Version (${MSVC_VERSION}) and CMAKE_SYSTEM_VERSION (${CMAKE_SYSTEM_VERSION}), limiting _WIN32_WINNT to ${ZLINK_WIN32_WINNT_LIMIT}, you may override this by setting ZLINK_WIN32_WINNT"
    )
    set(ZLINK_WIN32_WINNT_DEFAULT "${ZLINK_WIN32_WINNT_LIMIT}")
  endif()

  set(ZLINK_WIN32_WINNT
      "${ZLINK_WIN32_WINNT_DEFAULT}"
      CACHE STRING "Value to set _WIN32_WINNT to for building [default=autodetect from build environment]")

  add_definitions(-D_WIN32_WINNT=${ZLINK_WIN32_WINNT})
endif(WIN32)

# POLLER is always 'asio' (set earlier in ASIO configuration)
if(NOT POLLER STREQUAL "asio")
  message(FATAL_ERROR "POLLER must be 'asio' - other pollers are not supported")
endif()

message(STATUS "Using polling method in I/O threads: ${POLLER}")
# ASIO is the only supported I/O poller backend
set(ZLINK_IOTHREAD_POLLER_USE_ASIO 1)

# Add Asio poller source files when using Asio backend
if(POLLER STREQUAL "asio")
  list(APPEND sources
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/asio_poller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/asio_poller.hpp
    # ASIO-based TCP listener and connecter
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/asio_tcp_listener.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/asio_tcp_listener.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/asio_tcp_connecter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/asio_tcp_connecter.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/asio_ipc_connecter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/asio_ipc_connecter.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/asio_ipc_listener.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/asio_ipc_listener.hpp
    # Phase 1-C: ASIO engine (True Proactor Mode)
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/asio_engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/asio_engine.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/asio_zmp_engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/asio_zmp_engine.hpp
    # Transport abstraction layer (R3 refactoring)
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/i_asio_transport.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/tcp_transport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/ipc_transport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/tcp_transport.hpp
    # Utility headers (R2, R4 refactoring)
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/asio_debug.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/asio_error_handler.hpp)
endif()

# Phase 2: Add SSL transport when enabled
if(ZLINK_HAVE_ASIO_SSL)
  list(APPEND sources
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/ssl_transport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/ssl_transport.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/ssl_context_helper.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/ssl_context_helper.hpp
    # Phase 4: TLS connecter and listener with SSL/TLS encryption
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/asio_tls_connecter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/asio_tls_connecter.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/asio_tls_listener.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/asio_tls_listener.hpp)
endif()

# Phase 3: Add WebSocket transport when enabled
if(ZLINK_HAVE_ASIO_WS)
  list(APPEND sources
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/ws_transport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/ws_transport.hpp)
endif()

# Phase 3: Add WSS (Secure WebSocket) transport when both WS and SSL are enabled
if(ZLINK_HAVE_ASIO_WSS)
  list(APPEND sources
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/wss_transport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/wss_transport.hpp)
endif()

# Phase 3-B: Add WebSocket ZLINK integration files (listener, connecter, engine, address)
if(ZLINK_HAVE_WS)
  list(APPEND sources
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ws_address.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ws_address.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/asio_ws_listener.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/asio_ws_listener.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/asio_ws_connecter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/asio_ws_connecter.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/asio_ws_engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asio/asio_ws_engine.hpp)
endif()

# Phase 3-B: Add WSS ZLINK integration files
if(ZLINK_HAVE_WSS)
  list(APPEND sources
    ${CMAKE_CURRENT_SOURCE_DIR}/src/wss_address.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/wss_address.hpp)
endif()

# wepoll is not needed with ASIO poller

if(API_POLLER STREQUAL "")
  if(POLLER STREQUAL "select")
    set(API_POLLER "select")
  else()
    set(API_POLLER "poll")
  endif()
endif()

message(STATUS "Using polling method in zlink_poll(er)_* API: ${API_POLLER}")
string(TOUPPER ${API_POLLER} UPPER_API_POLLER)
set(ZLINK_POLL_BASED_ON_${UPPER_API_POLLER} 1)

check_cxx_symbol_exists(pselect sys/select.h HAVE_PSELECT)
if (NOT WIN32 AND HAVE_PSELECT)
  set(ZLINK_HAVE_PPOLL 1)
endif()

# special alignment settings
execute_process(
  COMMAND getconf LEVEL1_DCACHE_LINESIZE
  OUTPUT_VARIABLE CACHELINE_SIZE
  ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
if(CACHELINE_SIZE STREQUAL ""
   OR CACHELINE_SIZE EQUAL 0
   OR CACHELINE_SIZE EQUAL -1
   OR CACHELINE_SIZE EQUAL "undefined")
  set(ZLINK_CACHELINE_SIZE 64)
else()
  set(ZLINK_CACHELINE_SIZE ${CACHELINE_SIZE})
endif()
message(STATUS "Using ${ZLINK_CACHELINE_SIZE} bytes alignment for lock-free data structures")
check_cxx_symbol_exists(posix_memalign stdlib.h HAVE_POSIX_MEMALIGN)

if(NOT CYGWIN)
  # TODO cannot we simply do 'if(WIN32) set(ZLINK_HAVE_WINDOWS ON)' or similar?
  check_include_files(windows.h ZLINK_HAVE_WINDOWS)
endif()

if(NOT WIN32)
  set(ZLINK_HAVE_IPC 1)
  set(ZLINK_HAVE_STRUCT_SOCKADDR_UN 1)
else()
  # zlink: IPC is not supported on Windows.
  set(ZLINK_HAVE_IPC OFF)
endif()

# ##################### BEGIN condition_variable_t selection
if(NOT ZLINK_CV_IMPL)
  # prefer C++11 STL std::condition_variable implementation, if available
  check_include_files(condition_variable ZLINK_HAVE_STL_CONDITION_VARIABLE LANGUAGE CXX)

  if(ZLINK_HAVE_STL_CONDITION_VARIABLE)
    set(ZLINK_CV_IMPL_DEFAULT "stl11")
  else()
    if(WIN32 AND NOT CMAKE_SYSTEM_VERSION VERSION_LESS "6.0")
      # Win32API CONDITION_VARIABLE is supported from Windows Vista only
      set(ZLINK_CV_IMPL_DEFAULT "win32api")
    elseif(CMAKE_USE_PTHREADS_INIT)
      set(ZLINK_CV_IMPL_DEFAULT "pthreads")
    else()
      set(ZLINK_CV_IMPL_DEFAULT "none")
    endif()
  endif()

  # TODO a vxworks implementation also exists, but vxworks is not currently supported with cmake at all
  set(ZLINK_CV_IMPL
      "${ZLINK_CV_IMPL_DEFAULT}"
      CACHE STRING "Choose condition_variable_t implementation. Valid values are
       stl11, win32api, pthreads, none [default=autodetect]")
endif()

message(STATUS "Using condition_variable_t implementation: ${ZLINK_CV_IMPL}")
if(ZLINK_CV_IMPL STREQUAL "stl11")
  set(ZLINK_USE_CV_IMPL_STL11 1)
elseif(ZLINK_CV_IMPL STREQUAL "win32api")
  set(ZLINK_USE_CV_IMPL_WIN32API 1)
elseif(ZLINK_CV_IMPL STREQUAL "pthreads")
  set(ZLINK_USE_CV_IMPL_PTHREADS 1)
elseif(ZLINK_CV_IMPL STREQUAL "none")
  set(ZLINK_USE_CV_IMPL_NONE 1)
else()
  message(ERROR "Unknown value for ZLINK_CV_IMPL: ${ZLINK_CV_IMPL}")
endif()
# ##################### END condition_variable_t selection

if(NOT MSVC)
  check_include_files(ifaddrs.h ZLINK_HAVE_IFADDRS)
  check_include_files(sys/uio.h ZLINK_HAVE_UIO)
  check_include_files(sys/eventfd.h ZLINK_HAVE_EVENTFD)
  if(ZLINK_HAVE_EVENTFD AND NOT CMAKE_CROSSCOMPILING)
    zlink_check_efd_cloexec()
  endif()
endif()

if(ZLINK_HAVE_WINDOWS)
  # Cannot use check_library_exists because the symbol is always declared as char(*)(void)
  set(CMAKE_REQUIRED_LIBRARIES "ws2_32.lib")
  check_cxx_symbol_exists(WSAStartup "winsock2.h" HAVE_WS2_32)

  set(CMAKE_REQUIRED_LIBRARIES "rpcrt4.lib")
  check_cxx_symbol_exists(UuidCreateSequential "rpc.h" HAVE_RPCRT4)

  set(CMAKE_REQUIRED_LIBRARIES "iphlpapi.lib")
  check_cxx_symbol_exists(GetAdaptersAddresses "winsock2.h;iphlpapi.h" HAVE_IPHLAPI)
  check_cxx_symbol_exists(if_nametoindex "iphlpapi.h" HAVE_IF_NAMETOINDEX)

  set(CMAKE_REQUIRED_LIBRARIES "")
  # TODO: This not the symbol we're looking for. What is the symbol?
  check_library_exists(ws2 fopen "" HAVE_WS2)
else()
  check_cxx_symbol_exists(if_nametoindex net/if.h HAVE_IF_NAMETOINDEX)
  check_cxx_symbol_exists(SO_PEERCRED sys/socket.h ZLINK_HAVE_SO_PEERCRED)
  check_cxx_symbol_exists(LOCAL_PEERCRED sys/socket.h ZLINK_HAVE_LOCAL_PEERCRED)
  check_cxx_symbol_exists(SO_BUSY_POLL sys/socket.h ZLINK_HAVE_BUSY_POLL)
endif()

if(NOT MINGW)
  find_library(RT_LIBRARY rt)
  if(RT_LIBRARY)
    set(pkg_config_libs_private "${pkg_config_libs_private} -lrt")
  endif()
endif()

find_package(Threads)

if(WIN32 AND NOT CYGWIN)
  if(NOT HAVE_WS2_32 AND NOT HAVE_WS2)
    message(FATAL_ERROR "Cannot link to ws2_32 or ws2")
  endif()

  if(NOT HAVE_RPCRT4)
    message(FATAL_ERROR "Cannot link to rpcrt4")
  endif()

  if(NOT HAVE_IPHLAPI)
    message(FATAL_ERROR "Cannot link to iphlapi")
  endif()
endif()

if(NOT MSVC)
  set(CMAKE_REQUIRED_LIBRARIES rt)
  check_cxx_symbol_exists(clock_gettime time.h HAVE_CLOCK_GETTIME)
  set(CMAKE_REQUIRED_LIBRARIES)

  check_cxx_symbol_exists(fork unistd.h HAVE_FORK)
  check_cxx_symbol_exists(gethrtime sys/time.h HAVE_GETHRTIME)
  check_cxx_symbol_exists(mkdtemp stdlib.h HAVE_MKDTEMP)
  check_cxx_symbol_exists(accept4 sys/socket.h HAVE_ACCEPT4)
  check_cxx_symbol_exists(strnlen string.h HAVE_STRNLEN)
else()
  set(HAVE_STRNLEN 1)
endif()

add_definitions(-D_REENTRANT -D_THREAD_SAFE)
add_definitions(-DZLINK_CUSTOM_PLATFORM_HPP)

option(ENABLE_EVENTFD "Enable/disable eventfd" ZLINK_HAVE_EVENTFD)

macro(zlink_check_cxx_flag_prepend flag)
  check_cxx_compiler_flag("${flag}" HAVE_FLAG_${flag})

  if(HAVE_FLAG_${flag})
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${flag}")
  endif()
endmacro()

option(ENABLE_ANALYSIS "Build with static analysis(make take very long)" OFF)

if(MSVC)
  if(ENABLE_ANALYSIS)
    zlink_check_cxx_flag_prepend("/W4")

    zlink_check_cxx_flag_prepend("/analyze")

    # C++11/14/17-specific, but maybe possible via conditional defines
    zlink_check_cxx_flag_prepend("/wd26440") # Function '...' can be declared 'noexcept'
    zlink_check_cxx_flag_prepend("/wd26432") # If you define or delete any default operation in the type '...', define or
                                           # delete them all
    zlink_check_cxx_flag_prepend("/wd26439") # This kind of function may not throw. Declare it 'noexcept'
    zlink_check_cxx_flag_prepend("/wd26447") # The function is declared 'noexcept' but calls function '...' which may
                                           # throw exceptions
    zlink_check_cxx_flag_prepend("/wd26433") # Function '...' should be marked with 'override'
    zlink_check_cxx_flag_prepend("/wd26409") # Avoid calling new and delete explicitly, use std::make_unique<T> instead
    # Requires GSL
    zlink_check_cxx_flag_prepend("/wd26429") # Symbol '...' is never tested for nullness, it can be marked as not_null
    zlink_check_cxx_flag_prepend("/wd26446") # Prefer to use gsl::at()
    zlink_check_cxx_flag_prepend("/wd26481") # Don't use pointer arithmetic. Use span instead
    zlink_check_cxx_flag_prepend("/wd26472") # Don't use a static_cast for arithmetic conversions. Use brace
                                           # initialization, gsl::narrow_cast or gsl::narow
    zlink_check_cxx_flag_prepend("/wd26448") # Consider using gsl::finally if final action is intended
    zlink_check_cxx_flag_prepend("/wd26400") # Do not assign the result of an allocation or a function call with an
                                           # owner<T> return value to a raw pointer, use owner<T> instead
    zlink_check_cxx_flag_prepend("/wd26485") # Expression '...': No array to pointer decay(bounds.3)
  else()
    zlink_check_cxx_flag_prepend("/W3")
  endif()

  if(MSVC_IDE)
    set(MSVC_TOOLSET "-${CMAKE_VS_PLATFORM_TOOLSET}")
  else()
    set(MSVC_TOOLSET "")
  endif()
else()
  zlink_check_cxx_flag_prepend("-Wall")
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  zlink_check_cxx_flag_prepend("-Wextra")
endif()

option(LIBZLINK_PEDANTIC "" ON)
option(LIBZLINK_WERROR "" OFF)

# TODO: why is -Wno-long-long defined differently than in configure.ac?
if(NOT MSVC)
  zlink_check_cxx_flag_prepend("-Wno-long-long")
  zlink_check_cxx_flag_prepend("-Wno-uninitialized")

  if(LIBZLINK_PEDANTIC)
    zlink_check_cxx_flag_prepend("-pedantic")

    if(${CMAKE_CXX_COMPILER_ID} MATCHES "Intel")
      zlink_check_cxx_flag_prepend("-strict-ansi")
    endif()

    if(${CMAKE_CXX_COMPILER_ID} MATCHES "SunPro")
      zlink_check_cxx_flag_prepend("-compat=5")
    endif()
  endif()
endif()

if(LIBZLINK_WERROR)
  if(MSVC)
    zlink_check_cxx_flag_prepend("/WX")
  else()
    zlink_check_cxx_flag_prepend("-Werror")
    if(NOT "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
      zlink_check_cxx_flag_prepend("-errwarn=%all")
    endif()
  endif()
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^sparc")
  zlink_check_cxx_flag_prepend("-mcpu=v9")
endif()

if(${CMAKE_CXX_COMPILER_ID} MATCHES "SunPro")
  zlink_check_cxx_flag_prepend("-features=zla")
endif()

if(CMAKE_SYSTEM_NAME MATCHES "SunOS"
   OR CMAKE_SYSTEM_NAME MATCHES "NetBSD"
   OR CMAKE_SYSTEM_NAME MATCHES "QNX")
  message(STATUS "Checking whether atomic operations can be used")
  check_c_source_compiles(
    "\
  #include <atomic.h> \
  \
  int main() \
  { \
    uint32_t value; \
    atomic_cas_32(&value, 0, 0); \
    return 0; \
  } \
  "
    HAVE_ATOMIC_H)

  if(NOT HAVE_ATOMIC_H)
    set(ZLINK_FORCE_MUTEXES 1)
  endif()
endif()

if(NOT ANDROID)
  zlink_check_noexcept()
endif()

# -----------------------------------------------------------------------------
if (NOT MSVC)
  # Compilation checks
  zlink_check_pthread_setname()
  zlink_check_pthread_setaffinity()
  # Execution checks
  if(NOT CMAKE_CROSSCOMPILING)
    zlink_check_sock_cloexec()
    zlink_check_o_cloexec()
    zlink_check_so_bindtodevice()
    zlink_check_so_keepalive()
    zlink_check_so_priority()
    zlink_check_tcp_keepcnt()
    zlink_check_tcp_keepidle()
    zlink_check_tcp_keepintvl()
    zlink_check_tcp_keepalive()
    # zlink_check_tcp_tipc() removed - TIPC not supported
    zlink_check_getrandom()
  endif()
endif()

if(CMAKE_SYSTEM_NAME MATCHES "Linux"
   OR CMAKE_SYSTEM_NAME MATCHES "GNU/kFreeBSD"
   OR CMAKE_SYSTEM_NAME MATCHES "GNU/Hurd"
   OR CYGWIN)
  add_definitions(-D_GNU_SOURCE)
elseif(CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
  add_definitions(-D__BSD_VISIBLE)
elseif(CMAKE_SYSTEM_NAME MATCHES "NetBSD")
  add_definitions(-D_NETBSD_SOURCE)
elseif(CMAKE_SYSTEM_NAME MATCHES "OpenBSD")
  add_definitions(-D_OPENBSD_SOURCE)
elseif(CMAKE_SYSTEM_NAME MATCHES "SunOS")
  add_definitions(-D_PTHREADS)
elseif(CMAKE_SYSTEM_NAME MATCHES "HP-UX")
  add_definitions(-D_POSIX_C_SOURCE=200112L)
  zlink_check_cxx_flag_prepend(-Ae)
elseif(CMAKE_SYSTEM_NAME MATCHES "Darwin")
  add_definitions(-D_DARWIN_C_SOURCE)
endif()

find_package(AsciiDoc QUIET)

cmake_dependent_option(WITH_DOC "Build Reference Guide documentation(requires DocBook)" ON "ASCIIDOC_FOUND;NOT WIN32"
                       OFF) # Do not build docs on Windows due to issues with symlinks

# OpenPGM, NORM, and VMCI support removed (not needed with ASIO)

# -----------------------------------------------------------------------------
# force off-tree build

if(${CMAKE_CURRENT_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_BINARY_DIR})
  message(
    FATAL_ERROR
      "CMake generation is not allowed within the source directory! \
    Remove the CMakeCache.txt file and try again from another folder, e.g.: \
    \
      rm CMakeCache.txt \
      mkdir cmake-make \
      cd cmake-make \
      cmake ..")
endif()

# -----------------------------------------------------------------------------
# default to Release build

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  # CMAKE_BUILD_TYPE is not used for multi-configuration generators like Visual Studio/XCode which instead use
  # CMAKE_CONFIGURATION_TYPES
  set(CMAKE_BUILD_TYPE
      Release
      CACHE STRING "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif()

# -----------------------------------------------------------------------------
# output directories

zlink_set_with_default(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(UNIX)
  set(zlink_library_directory "lib")
else()
  set(zlink_library_directory "bin")
endif()
zlink_set_with_default(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${zlink_library_directory}")
zlink_set_with_default(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# -----------------------------------------------------------------------------
# platform specifics

if(WIN32)
  # Socket limit is 16K(can be raised arbitrarily)
  add_definitions(-DFD_SETSIZE=16384)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
  add_definitions(-D_WINSOCK_DEPRECATED_NO_WARNINGS)
endif()

if(MSVC)
  # Allow C++ standard override via ZLINK_CXX_STANDARD option
  set(ZLINK_CXX_STANDARD "17" CACHE STRING "C++ standard to use (11, 14, 17, 20, etc.).")

  if(ZLINK_CXX_STANDARD)
    if(ZLINK_CXX_STANDARD STREQUAL "11")
      # MSVC 2015+ defaults to C++14, explicit C++11 not needed
      message(STATUS "Using C++11 standard (MSVC default)")
    elseif(ZLINK_CXX_STANDARD STREQUAL "14")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++14")
      message(STATUS "Using C++14 standard")
    elseif(ZLINK_CXX_STANDARD STREQUAL "17")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++17")
      message(STATUS "Using C++17 standard")
    elseif(ZLINK_CXX_STANDARD STREQUAL "20")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++20")
      message(STATUS "Using C++20 standard")
    elseif(ZLINK_CXX_STANDARD STREQUAL "latest")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++latest")
      message(STATUS "Using C++ latest standard")
    else()
      message(WARNING "Unsupported C++ standard ${ZLINK_CXX_STANDARD} for MSVC")
    endif()
  endif()

  # Parallel make.
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MP")

  # Compile the static lib with debug information included note: we assume here that the default flags contain some /Z
  # flag
  string(REGEX REPLACE "/Z.[^:]" "/Z7 " CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
  string(REGEX REPLACE "/Z.[^:]" "/Z7 " CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")

endif()

# -----------------------------------------------------------------------------
# source files

set(api-sources
    src/api/zlink.cpp
    src/api/zlink_utils.cpp)

set(core-sources
    src/core/msg.cpp
    src/core/ctx.cpp
    src/core/pipe.cpp
    src/core/mailbox.cpp
    src/core/mailbox_safe.cpp
    src/core/object.cpp
    src/core/own.cpp
    src/core/io_object.cpp
    src/core/io_thread.cpp
    src/core/reaper.cpp
    src/core/signaler.cpp
    src/core/thread.cpp
    src/core/timers.cpp
    src/core/options.cpp
    src/core/poller_base.cpp
    src/core/socket_poller.cpp
    src/core/session_base.cpp
    src/core/address.cpp
    src/core/endpoint.cpp
    src/core/platform_thread.cpp)

set(protocol-sources
    src/protocol/zmp_decoder.cpp
    src/protocol/zmp_encoder.cpp
    src/protocol/raw_decoder.cpp
    src/protocol/raw_encoder.cpp
    src/protocol/metadata.cpp
    src/protocol/decoder_allocators.cpp)

set(discovery-sources
    src/discovery/registry.cpp
    src/discovery/discovery.cpp
    src/discovery/gateway.cpp
    src/discovery/provider.cpp)

set(spot-sources
    src/spot/spot_node.cpp
    src/spot/spot.cpp)

set(socket-sources
    src/sockets/socket_base.cpp
    src/sockets/thread_safe_socket.cpp
    src/sockets/dealer.cpp
    src/sockets/router.cpp
    src/sockets/stream.cpp
    src/sockets/pub.cpp
    src/sockets/sub.cpp
    src/sockets/xpub.cpp
    src/sockets/xsub.cpp
    src/sockets/pair.cpp
    src/sockets/lb.cpp
    src/sockets/fq.cpp
    src/sockets/dist.cpp
    src/sockets/proxy.cpp)

set(engine-sources
    src/engine/asio/asio_engine.cpp
    src/engine/asio/asio_poller.cpp
    src/engine/asio/asio_zmp_engine.cpp
    src/engine/asio/asio_raw_engine.cpp)

set(transport-sources
    src/transports/tcp/tcp.cpp
    src/transports/tcp/tcp_address.cpp
    src/transports/tcp/asio_tcp_listener.cpp
    src/transports/tcp/asio_tcp_connecter.cpp
    src/transports/tcp/tcp_transport.cpp
    src/transports/ipc/ipc_address.cpp
    src/transports/ipc/ipc_transport.cpp
    src/transports/ipc/asio_ipc_listener.cpp
    src/transports/ipc/asio_ipc_connecter.cpp
    src/transports/ws/ws_address.cpp
    src/transports/ws/ws_transport.cpp
    src/transports/ws/asio_ws_listener.cpp
    src/transports/ws/asio_ws_connecter.cpp
    src/transports/ws/asio_ws_engine.cpp
    src/transports/tls/wss_address.cpp
    src/transports/tls/wss_transport.cpp
    src/transports/tls/ssl_context_helper.cpp
    src/transports/tls/ssl_transport.cpp
    src/transports/tls/asio_tls_listener.cpp
    src/transports/tls/asio_tls_connecter.cpp)

set(pgm-sources
    src/transports/pgm/pgm_socket.cpp
    src/transports/pgm/pgm_sender.cpp
    src/transports/pgm/pgm_receiver.cpp)

set(utils-sources
    src/utils/allocator.cpp
    src/utils/clock.cpp
    src/utils/err.cpp
    src/utils/ip.cpp
    src/utils/ip_resolver.cpp
    src/utils/mtrie.cpp
    src/utils/polling_util.cpp
    src/utils/precompiled.cpp
    src/utils/random.cpp
    src/utils/trie.cpp
    src/utils/radix_tree.cpp)

set(sources
    ${api-sources}
    ${core-sources}
    ${protocol-sources}
    ${discovery-sources}
    ${spot-sources}
    ${socket-sources}
    ${engine-sources}
    ${transport-sources}
    ${utils-sources})

if(ZLINK_HAVE_OPENPGM)
  list(APPEND sources ${pgm-sources})
endif()

# Add all subdirectories to include path to maintain compatibility with existing flat includes
include_directories(
    src
    src/api
    src/core
    src/protocol
    src/discovery
    src/spot
    src/engine
    src/engine/asio
    src/transports/tcp
    src/transports/ipc
    src/transports/ws
    src/transports/tls
    src/transports/pgm
    src/sockets
    src/utils
)

if(MINGW)
  # Generate the right type when using -m32 or -m64
  macro(set_rc_arch rc_target)
    set(CMAKE_RC_COMPILER_INIT windres)
    enable_language(RC)
    set(CMAKE_RC_COMPILE_OBJECT
        "<CMAKE_RC_COMPILER> <FLAGS> -O coff --target=${rc_target} <DEFINES> -i <SOURCE> -o <OBJECT>")
  endmacro()

  if(NOT CMAKE_SYSTEM_PROCESSOR)
    set(CMAKE_SYSTEM_PROCESSOR ${CMAKE_HOST_SYSTEM_PROCESSOR})
  endif()

  # Also happens on x86_64 systems...what a worthless variable
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "i386"
     OR CMAKE_SYSTEM_PROCESSOR MATCHES "i486"
     OR CMAKE_SYSTEM_PROCESSOR MATCHES "i586"
     OR CMAKE_SYSTEM_PROCESSOR MATCHES "i686"
     OR CMAKE_SYSTEM_PROCESSOR MATCHES "x86"
     OR CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64"
     OR CMAKE_SYSTEM_PROCESSOR MATCHES "amd64")

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
      set_rc_arch("pe-x86-64")
    else()
      set_rc_arch("pe-i386")
    endif()
  endif()
endif()

set(public_headers include/zlink.h include/zlink_utils.h include/zlink_threadsafe.h)

set(readme-docs AUTHORS LICENSE NEWS)

# -----------------------------------------------------------------------------
# optional modules

# OpenPGM, NORM, VMCI, and TIPC support removed

# -----------------------------------------------------------------------------
# source generators

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/version.rc.in ${CMAKE_CURRENT_BINARY_DIR}/version.rc)

# Delete any src/platform.hpp left by configure
file(REMOVE ${CMAKE_CURRENT_SOURCE_DIR}/src/platform.hpp)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/builds/cmake/platform.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/platform.hpp)
list(APPEND sources ${CMAKE_CURRENT_BINARY_DIR}/platform.hpp)

set(prefix ${CMAKE_INSTALL_PREFIX})
set(exec_prefix ${prefix})
set(libdir ${prefix}/${CMAKE_INSTALL_LIBDIR})
set(includedir ${prefix}/${CMAKE_INSTALL_INCLUDEDIR})
set(VERSION ${ZLINK_VERSION_MAJOR}.${ZLINK_VERSION_MINOR}.${ZLINK_VERSION_PATCH})
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/libzlink.pc.in ${CMAKE_CURRENT_BINARY_DIR}/libzlink.pc @ONLY)
set(zlink-pkgconfig ${CMAKE_CURRENT_BINARY_DIR}/libzlink.pc)

if(NOT ZLINK_BUILD_FRAMEWORK)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libzlink.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
endif()

if(MSVC)
  if(CMAKE_CL_64)
    set(nsis-template ${CMAKE_CURRENT_SOURCE_DIR}/builds/cmake/NSIS.template64.in)
  else()
    set(nsis-template ${CMAKE_CURRENT_SOURCE_DIR}/builds/cmake/NSIS.template32.in)
  endif()

  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/NSIS.template.in
    COMMAND ${CMAKE_COMMAND} ARGS -E copy ${nsis-template} ${CMAKE_CURRENT_BINARY_DIR}/NSIS.template.in
    DEPENDS ${nsis-template})
endif()

option(WITH_DOCS "Build html docs" ON)
if(WITH_DOCS)
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/doc)
  file(
    GLOB docs
    RELATIVE ${CMAKE_CURRENT_BINARY_DIR}/
    "${CMAKE_CURRENT_SOURCE_DIR}/doc/*.txt")
  set(html-docs)
  foreach(txt ${docs})
    string(REGEX REPLACE ".*/(.*)\\.txt" "\\1.html" html ${txt})
    set(src ${txt})
    set(dst doc/${html})
    if(WITH_DOC)
      add_custom_command(
        OUTPUT ${dst}
        COMMAND ${ASCIIDOC_EXECUTABLE} -d manpage -b xhtml11 -f ${CMAKE_CURRENT_SOURCE_DIR}/doc/asciidoc.conf
                -azlink_version=${ZLINK_VERSION} -o ${dst} ${src}
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${src}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating ${html}")
      list(APPEND html-docs ${CMAKE_CURRENT_BINARY_DIR}/${dst})
    endif()
  endforeach()
endif()

if(ZLINK_BUILD_FRAMEWORK)
  add_custom_command(
    TARGET libzlink
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} ARGS -E make_directory
            "${CMAKE_LIBRARY_OUTPUT_PATH}/Zlink.framework/Versions/${ZLINK_VERSION}/MacOS"
    COMMENT "Perf tools")
endif()

option(ENABLE_PRECOMPILED "Enable precompiled headers, if possible" OFF)



if(MSVC AND ENABLE_PRECOMPILED)

  # default for all sources is to use precompiled headers

  foreach(source ${sources})

    # C and C++ can not use the same precompiled header

    if(${source} MATCHES ".cpp$" AND NOT ${source} STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/src/utils/precompiled.cpp")

      set_source_files_properties(${source} PROPERTIES COMPILE_FLAGS "/Yuprecompiled.hpp" OBJECT_DEPENDS

                                                                                          precompiled.hpp)

    endif()

  endforeach()

  # create precompiled header

  set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/src/utils/precompiled.cpp

                              PROPERTIES COMPILE_FLAGS "/Ycprecompiled.hpp" OBJECT_OUTPUTS precompiled.hpp)

endif()

# -----------------------------------------------------------------------------
# output
option(BUILD_SHARED "Whether or not to build the shared object" ON)
option(BUILD_STATIC "Whether or not to build the static archive" ON)

if(MSVC)
  # Suppress linker warnings caused by #ifdef omission of file content.
  set(CMAKE_STATIC_LINKER_FLAGS "${CMAKE_STATIC_LINKER_FLAGS} /ignore:4221")
  set(PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
  set(PDB_NAME
      "lib${ZLINK_OUTPUT_BASENAME}${MSVC_TOOLSET}-mt-gd-${ZLINK_VERSION_MAJOR}_${ZLINK_VERSION_MINOR}_${ZLINK_VERSION_PATCH}")
  function(enable_vs_guideline_checker target)
    set_target_properties(
      ${target} PROPERTIES VS_GLOBAL_EnableCppCoreCheck true VS_GLOBAL_CodeAnalysisRuleSet CppCoreCheckRules.ruleset
                           VS_GLOBAL_RunCodeAnalysis true)
  endfunction()
  if(BUILD_SHARED)
    # Whole Program Optimization flags. http://msdn.microsoft.com/en-us/magazine/cc301698.aspx
    #
    # "Finally, there's the subject of libraries. It's possible to create .LIB 
    # files with code in its IL form. The linker will happily work with these 
    # .LIB files. Be aware that these libraries will be tied to a specific 
    # version of the compiler and linker. If you distribute these libraries, 
    # you'll need to update them if Microsoft changes the format of IL in a
    # future release."
    #
    # Note: Manual /GL and /LTCG flags replaced with CMake's native
    # INTERPROCEDURAL_OPTIMIZATION (see line ~100 for ENABLE_LTO option).

    add_library(libzlink SHARED ${sources} ${public_headers} ${html-docs} ${readme-docs}
                              ${CMAKE_CURRENT_BINARY_DIR}/NSIS.template.in ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
    if(ENABLE_ANALYSIS)
      enable_vs_guideline_checker(libzlink)
    endif()
    set_target_properties(
      libzlink
      PROPERTIES PUBLIC_HEADER "${public_headers}"
                 RELEASE_POSTFIX "${MSVC_TOOLSET}-mt-${ZLINK_VERSION_MAJOR}_${ZLINK_VERSION_MINOR}_${ZLINK_VERSION_PATCH}"
                 RELWITHDEBINFO_POSTFIX
                 "${MSVC_TOOLSET}-mt-${ZLINK_VERSION_MAJOR}_${ZLINK_VERSION_MINOR}_${ZLINK_VERSION_PATCH}"
                 MINSIZEREL_POSTFIX "${MSVC_TOOLSET}-mt-${ZLINK_VERSION_MAJOR}_${ZLINK_VERSION_MINOR}_${ZLINK_VERSION_PATCH}"
                 DEBUG_POSTFIX "${MSVC_TOOLSET}-mt-gd-${ZLINK_VERSION_MAJOR}_${ZLINK_VERSION_MINOR}_${ZLINK_VERSION_PATCH}"
                 RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
                 COMPILE_DEFINITIONS "DLL_EXPORT"
                 OUTPUT_NAME "lib${ZLINK_OUTPUT_BASENAME}")
    if(ZLINK_HAVE_WINDOWS_UWP)
      set_target_properties(libzlink PROPERTIES LINK_FLAGS_DEBUG "/OPT:NOICF /OPT:NOREF")
    endif()
  endif()

  if(BUILD_STATIC)
    add_library(libzlink-static STATIC ${sources} ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
    set_target_properties(
      libzlink-static
      PROPERTIES PUBLIC_HEADER "${public_headers}"
                 RELEASE_POSTFIX "${MSVC_TOOLSET}-mt-s-${ZLINK_VERSION_MAJOR}_${ZLINK_VERSION_MINOR}_${ZLINK_VERSION_PATCH}"
                 RELWITHDEBINFO_POSTFIX
                 "${MSVC_TOOLSET}-mt-s-${ZLINK_VERSION_MAJOR}_${ZLINK_VERSION_MINOR}_${ZLINK_VERSION_PATCH}"
                 MINSIZEREL_POSTFIX
                 "${MSVC_TOOLSET}-mt-s-${ZLINK_VERSION_MAJOR}_${ZLINK_VERSION_MINOR}_${ZLINK_VERSION_PATCH}"
                 DEBUG_POSTFIX "${MSVC_TOOLSET}-mt-sgd-${ZLINK_VERSION_MAJOR}_${ZLINK_VERSION_MINOR}_${ZLINK_VERSION_PATCH}"
                 COMPILE_FLAGS "/DZLINK_STATIC"
                 OUTPUT_NAME "lib${ZLINK_OUTPUT_BASENAME}")
  endif()
else()
  # avoid building everything twice for shared + static only on *nix, as Windows needs different preprocessor defines in
  # static builds
  if(NOT MINGW)
    add_library(objects OBJECT ${sources})
    if(ZLINK_HAVE_ASIO_SSL)
      target_link_libraries(objects PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    endif()
    set_property(TARGET objects PROPERTY POSITION_INDEPENDENT_CODE ON)
  endif()

  if(BUILD_SHARED)
    if(MINGW)
      add_library(libzlink SHARED ${sources} ${public_headers} ${html-docs} ${readme-docs} ${zlink-pkgconfig}
                                ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
    else()
        if (CMAKE_GENERATOR STREQUAL "Xcode")
           add_library(libzlink SHARED ${sources} ${public_headers} ${html-docs} ${readme-docs}
                                     ${zlink-pkgconfig} ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
        else()
           add_library(libzlink SHARED $<TARGET_OBJECTS:objects> ${public_headers} ${html-docs} ${readme-docs}
                                     ${zlink-pkgconfig} ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
        endif()

    endif()
    # NOTE: the SOVERSION and VERSION MUST be the same as the one generated by libtool! It is NOT the same as the
    # version of the package.
    set_target_properties(
      libzlink PROPERTIES COMPILE_DEFINITIONS "DLL_EXPORT" PUBLIC_HEADER "${public_headers}" VERSION "5.2.5"
                        SOVERSION "5" OUTPUT_NAME "${ZLINK_OUTPUT_BASENAME}" PREFIX "lib")
    if(ZLINK_BUILD_FRAMEWORK)
      set_target_properties(
        libzlink
        PROPERTIES FRAMEWORK TRUE MACOSX_FRAMEWORK_IDENTIFIER "org.zlink.libzlink" MACOSX_FRAMEWORK_SHORT_VERSION_STRING
                                                                                  ${ZLINK_VERSION}
                   MACOSX_FRAMEWORK_BUNDLE_VERSION ${ZLINK_VERSION})
      set_source_files_properties(${html-docs} PROPERTIES MACOSX_PACKAGE_LOCATION doc)
      set_source_files_properties(${readme-docs} PROPERTIES MACOSX_PACKAGE_LOCATION etc)
      set_source_files_properties(${zlink-pkgconfig} PROPERTIES MACOSX_PACKAGE_LOCATION lib/pkgconfig)
    endif()
  endif()

  if(BUILD_STATIC)
    if(MINGW)
      add_library(libzlink-static STATIC ${sources} ${public_headers} ${html-docs} ${readme-docs} ${zlink-pkgconfig}
                                       ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
    else()
      if (CMAKE_GENERATOR STREQUAL "Xcode")
        add_library(libzlink-static STATIC ${sources} ${public_headers} ${html-docs} ${readme-docs}
                                         ${zlink-pkgconfig} ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
      else()
        add_library(libzlink-static STATIC $<TARGET_OBJECTS:objects> ${public_headers} ${html-docs} ${readme-docs}
                                         ${zlink-pkgconfig} ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
      endif()
    endif()
    if(CMAKE_SYSTEM_NAME MATCHES "QNX")
      target_link_libraries(libzlink-static m)
    endif()
    set_target_properties(
      libzlink-static PROPERTIES PUBLIC_HEADER "${public_headers}" OUTPUT_NAME "${ZLINK_OUTPUT_BASENAME}" PREFIX "lib")
  endif()
endif()

if(BUILD_STATIC)
  target_compile_definitions(libzlink-static PUBLIC ZLINK_STATIC)
endif()

list(APPEND target_outputs "")

if(BUILD_SHARED)
  list(APPEND target_outputs "libzlink")
endif()

if(BUILD_STATIC)
  list(APPEND target_outputs "libzlink-static")
endif()

set(build_targets ${target_outputs})
if(TARGET objects)
  list(APPEND build_targets "objects")
endif()

# Ensure ASIO configuration macros are consistent across all targets.
if(ZLINK_HAVE_ASIO)
  add_compile_definitions(BOOST_ASIO_STANDALONE=1 BOOST_ASIO_NO_DEPRECATED=1)
endif()

foreach(target ${build_targets})
  target_include_directories(
    ${target} PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                     $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}> $<INSTALL_INTERFACE:include>)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

  # ASIO is mandatory - always include headers and definitions
  if(ZLINK_HAVE_ASIO)
    target_include_directories(${target} PRIVATE ${BUNDLED_BOOST_INCLUDE_DIR})
    # Use standalone Asio mode (no dependency on other Boost libraries)
    target_compile_definitions(${target} PRIVATE
      BOOST_ASIO_STANDALONE=1
      BOOST_ASIO_NO_DEPRECATED=1)
  endif()
endforeach()

if(BUILD_SHARED)
  target_link_libraries(libzlink ${CMAKE_THREAD_LIBS_INIT})

  if(QNX)
    target_link_libraries(libzlink -lsocket)
  endif()

  if(ZLINK_HAVE_ASIO_SSL)
    target_link_libraries(libzlink OpenSSL::SSL OpenSSL::Crypto)
    if(WIN32)
      target_link_libraries(libzlink crypt32)
    endif()
  endif()

  if(LIBBSD_FOUND)
    target_link_libraries(libzlink ${LIBBSD_LIBRARIES})
  endif()

  if(ZLINK_HAVE_OPENPGM)
    target_link_libraries(libzlink ${OPENPGM_LIBRARIES})
  endif()


  if(HAVE_WS2_32)
    target_link_libraries(libzlink ws2_32)
  elseif(HAVE_WS2)
    target_link_libraries(libzlink ws2)
  endif()

  if(HAVE_RPCRT4)
    target_link_libraries(libzlink rpcrt4)
  endif()

  if(HAVE_IPHLAPI)
    target_link_libraries(libzlink iphlpapi)
  endif()

  if(RT_LIBRARY)
    target_link_libraries(libzlink -lrt)
  endif()
endif()

if(BUILD_STATIC)
  target_link_libraries(libzlink-static ${CMAKE_THREAD_LIBS_INIT})
  if(ZLINK_HAVE_ASIO_SSL)
    target_link_libraries(libzlink-static OpenSSL::SSL OpenSSL::Crypto)
    if(WIN32)
      target_link_libraries(libzlink-static crypt32)
    endif()
  endif()

  if(LIBBSD_FOUND)
    target_link_libraries(libzlink-static ${LIBBSD_LIBRARIES})
  endif()

  if(ZLINK_HAVE_OPENPGM)
    target_link_libraries(libzlink-static ${OPENPGM_LIBRARIES})
  endif()


  if(HAVE_WS2_32)
    target_link_libraries(libzlink-static ws2_32)
  elseif(HAVE_WS2)
    target_link_libraries(libzlink-static ws2)
  endif()

  if(HAVE_RPCRT4)
    target_link_libraries(libzlink-static rpcrt4)
  endif()

  if(HAVE_IPHLAPI)
    target_link_libraries(libzlink-static iphlpapi)
  endif()

  if(RT_LIBRARY)
    target_link_libraries(libzlink-static -lrt)
  endif()

  if(CMAKE_SYSTEM_NAME MATCHES "QNX")
    add_definitions(-DUNITY_EXCLUDE_MATH_H)
  endif()
endif()

# -----------------------------------------------------------------------------
# tests

if(${CMAKE_VERSION} VERSION_LESS 3.12.3)
  option(BUILD_TESTS "Whether or not to build the tests" OFF)
else()
  option(BUILD_TESTS "Whether or not to build the tests" ON)
endif()

set(ZLINK_BUILD_TESTS
    ${BUILD_TESTS}
    CACHE BOOL "Build the tests for Zlink")

if(ZLINK_BUILD_TESTS)
  enable_testing() # Enable testing only works in root scope
  add_subdirectory(tests)
  if(BUILD_STATIC)
    add_subdirectory(unittests)
  else()
    message(WARNING "Not building unit tests, since BUILD_STATIC is not enabled")
  endif()
endif()

# -----------------------------------------------------------------------------
# benchmarks
option(BUILD_BENCHMARKS "Build benchmark tools for performance comparison" OFF)
if(BUILD_BENCHMARKS AND BUILD_SHARED)
  add_subdirectory(benchwithzlink-baseline)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/benchwithzmq/CMakeLists.txt")
    add_subdirectory(benchwithzmq)
  endif()
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/benchwithzlink/CMakeLists.txt")
    add_subdirectory(benchwithzlink)
  endif()
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/benchwithbeast/CMakeLists.txt")
    add_subdirectory(benchwithbeast)
  endif()
endif()

# -----------------------------------------------------------------------------
# installer

if(MSVC AND (BUILD_SHARED OR BUILD_STATIC))
  install(
    TARGETS ${target_outputs}
    EXPORT ${PROJECT_NAME}-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT SDK)
  if(MSVC_IDE)
    install(
      FILES ${PDB_OUTPUT_DIRECTORY}/\${CMAKE_INSTALL_CONFIG_NAME}/${PDB_NAME}.pdb
      DESTINATION ${CMAKE_INSTALL_BINDIR}
      COMPONENT SDK
      OPTIONAL)
  else()
    install(
      FILES ${PDB_OUTPUT_DIRECTORY}/${PDB_NAME}.pdb
      DESTINATION ${CMAKE_INSTALL_BINDIR}
      COMPONENT SDK
      OPTIONAL)
  endif()
  if(BUILD_SHARED)
    install(
      TARGETS libzlink
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
      PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT Runtime)
  endif()
elseif(BUILD_SHARED OR BUILD_STATIC)
  install(
    TARGETS ${target_outputs}
    EXPORT ${PROJECT_NAME}-targets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FRAMEWORK DESTINATION "Library/Frameworks"
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()

foreach(readme ${readme-docs})
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/${readme} ${CMAKE_CURRENT_BINARY_DIR}/${readme}.txt)

  if(NOT ZLINK_BUILD_FRAMEWORK)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${readme}.txt DESTINATION share/zlink)
  endif()
endforeach()

if(WITH_DOC)
  if(NOT ZLINK_BUILD_FRAMEWORK)
    install(
      FILES ${html-docs}
      DESTINATION doc/zlink
      COMPONENT RefGuide)
  endif()
endif()

if(WIN32)
  set(ZEROMQ_CMAKECONFIG_INSTALL_DIR
      "CMake"
      CACHE STRING "install path for ZlinkConfig.cmake")
else()
  # CMake search path wants either "share" (AKA GNUInstallDirs DATAROOTDIR) for arch-independent, or LIBDIR for arch-
  # dependent, plus "cmake" as prefix
  set(ZEROMQ_CMAKECONFIG_INSTALL_DIR
      "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
      CACHE STRING "install path for ZlinkConfig.cmake")
endif()

if((NOT CMAKE_VERSION VERSION_LESS 3.0) AND (BUILD_SHARED OR BUILD_STATIC))
  export(EXPORT ${PROJECT_NAME}-targets FILE "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Targets.cmake")
endif()
configure_package_config_file(
  builds/cmake/ZlinkConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
  INSTALL_DESTINATION ${ZEROMQ_CMAKECONFIG_INSTALL_DIR})
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
  VERSION ${ZLINK_VERSION_MAJOR}.${ZLINK_VERSION_MINOR}.${ZLINK_VERSION_PATCH}
  COMPATIBILITY AnyNewerVersion)
if(BUILD_SHARED OR BUILD_STATIC)
  install(
    EXPORT ${PROJECT_NAME}-targets
    FILE ${PROJECT_NAME}Targets.cmake
    DESTINATION ${ZEROMQ_CMAKECONFIG_INSTALL_DIR})
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
                ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
          DESTINATION ${ZEROMQ_CMAKECONFIG_INSTALL_DIR})
endif()

option(ENABLE_CPACK "Enables cpack rules" ON)
if(MSVC AND ENABLE_CPACK)
  if(${CMAKE_BUILD_TYPE} MATCHES "Debug")
    set(CMAKE_INSTALL_DEBUG_LIBRARIES_ONLY TRUE)
    set(CMAKE_INSTALL_DEBUG_LIBRARIES TRUE)
    set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
  endif()
  include(InstallRequiredSystemLibraries)

  if(CMAKE_CL_64)
    set(arch_name "x64")
  else()
    set(arch_name "x86")
  endif()

  set(CPACK_NSIS_DISPLAY_NAME "Zlink ${ZLINK_VERSION_MAJOR}.${ZLINK_VERSION_MINOR}.${ZLINK_VERSION_PATCH}(${arch_name})")
  set(CPACK_PACKAGE_FILE_NAME "Zlink-${ZLINK_VERSION_MAJOR}.${ZLINK_VERSION_MINOR}.${ZLINK_VERSION_PATCH}-${arch_name}")

  # TODO: I think this part was intended to be used when running cpack separately from cmake but I don't know how that
  # works.
  #
  # macro(add_crt_version version) set(rel_dir
  # "${CMAKE_CURRENT_BINARY_DIR}/build/${arch_name}/${version};Zlink;ALL;/")
  # set(debug_dir
  # "${CMAKE_CURRENT_BINARY_DIR}/debug/${arch_name}/${version};Zlink;ALL;/")
  # if(EXISTS ${rel_dir}) list(APPEND CPACK_INSTALL_CMAKE_PROJECTS ${rel_dir}) endif()

  # if(EXISTS ${debug_dir}) list(APPEND CPACK_INSTALL_CMAKE_PROJECTS ${rel_dir}) endmacro() endmacro()

  # add_crt_version(v110) add_crt_version(v100) add_crt_version(v90)

  list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_BINARY_DIR})
  set(CPACK_GENERATOR "NSIS")
  set(CPACK_PACKAGE_NAME "Zlink")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Zlink lightweight messaging kernel")
  set(CPACK_PACKAGE_VENDOR "Miru")
  set(CPACK_NSIS_CONTACT "Steven McCoy <Steven.McCoy@miru.hk>")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_BINARY_DIR}\\\\LICENSE.txt")
  # set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_BINARY_DIR}\\\\README.txt") set(CPACK_RESOURCE_FILE_WELCOME
  # "${CMAKE_CURRENT_BINARY_DIR}\\\\WELCOME.txt") There is a bug in NSI that does not handle full unix paths properly.
  # Make sure there is at least one set of four(4) backslashes.
  set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}\\\\installer.ico")
  set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}\\\\installer.ico")

  set(CPACK_PACKAGE_ICON "${CMAKE_CURRENT_SOURCE_DIR}\\\\branding.bmp")
  set(CPACK_NSIS_COMPRESSOR "/SOLID lzma")
  set(CPACK_PACKAGE_VERSION ${ZLINK_VERSION})
  set(CPACK_PACKAGE_VERSION_MAJOR ${ZLINK_VERSION_MAJOR})
  set(CPACK_PACKAGE_VERSION_MINOR ${ZLINK_VERSION_MINOR})
  set(CPACK_PACKAGE_VERSION_PATCH ${ZLINK_VERSION_PATCH})
  # set(CPACK_PACKAGE_INSTALL_DIRECTORY "ZLINK Install Directory") set(CPACK_TEMPORARY_DIRECTORY "ZLINK Temporary CPack
  # Directory")

  include(CPack)

  cpack_add_component_group(Development DISPLAY_NAME "Zlink software development kit" EXPANDED)
  cpack_add_component(PerfTools DISPLAY_NAME "Zlink performance tools" INSTALL_TYPES FullInstall DevInstall)
  cpack_add_component(SourceCode DISPLAY_NAME "Zlink source code" DISABLED INSTALL_TYPES FullInstall)
  cpack_add_component(
    SDK
    DISPLAY_NAME
    "Zlink headers and libraries"
    INSTALL_TYPES
    FullInstall
    DevInstall
    GROUP
    Development)
  if(WITH_DOC)
    cpack_add_component(
      RefGuide
      DISPLAY_NAME
      "Zlink reference guide"
      INSTALL_TYPES
      FullInstall
      DevInstall
      GROUP
      Development)
  endif()
  cpack_add_component(
    Runtime
    DISPLAY_NAME
    "Zlink runtime files"
    REQUIRED
    INSTALL_TYPES
    FullInstall
    DevInstall
    MinInstall)
  cpack_add_install_type(FullInstall DISPLAY_NAME "Full install, including source code")
  cpack_add_install_type(DevInstall DISPLAY_NAME "Developer install, headers and libraries")
  cpack_add_install_type(MinInstall DISPLAY_NAME "Minimal install, runtime only")
endif()

# Export this for library to help build this as a sub-project
set(ZEROMQ_LIBRARY
    libzlink
    CACHE STRING "Zlink library")

# Workaround for MSVS10 to avoid the Dialog Hell FIXME: This could be removed with future version of CMake.
if(MSVC_VERSION EQUAL 1600)
  set(ZLINK_SLN_FILENAME "${CMAKE_CURRENT_BINARY_DIR}/Zlink.sln")
  if(EXISTS "${ZLINK_SLN_FILENAME}")
    file(APPEND "${ZLINK_SLN_FILENAME}" "\n# This should be regenerated!\n")
  endif()
endif()

# this cannot be moved, as it does not only contain function/macro definitions
option(ENABLE_CLANG "Include Clang" ON)
if (ENABLE_CLANG)
  include(ClangFormat)
endif()

# fixes https://github.com/zlink/libzlink/issues/3776 The problem is, both libzlink-static libzlink try to use/generate
# precompiled.pch at the same time Add a dependency, so they run in order and so they dont get in each others way TODO
# still generates warning "build\x64-Debug\ninja : warning : multiple rules generate precompiled.hpp. builds involving
# this target will not be correct; continuing anyway [-w dupbuild=warn]"
if(MSVC
   AND ENABLE_PRECOMPILED
   AND BUILD_SHARED
   AND BUILD_STATIC)
  add_dependencies(libzlink-static libzlink)
endif()

option(ENABLE_NO_EXPORT "Build with empty ZLINK_EXPORT macro, bypassing platform-based automated detection" OFF)
if(ENABLE_NO_EXPORT)
  message(STATUS "Building with empty ZLINK_EXPORT macro")
  add_definitions(-DZLINK_NO_EXPORT)
endif()
