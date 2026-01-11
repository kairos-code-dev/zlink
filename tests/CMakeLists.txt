cmake_minimum_required(VERSION 3.15)
project(libzmq-tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Use static MSVC runtime on Windows to match libzmq build
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Detect platform and set library paths
if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(PLATFORM_DIR "windows-x64")
    else()
        set(PLATFORM_DIR "windows-x86")
    endif()
    set(ZMQ_LIB_NAME "libzmq.dll")
    set(ZMQ_IMPORT_LIB "libzmq.lib")
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(PLATFORM_DIR "macos-arm64")
    else()
        set(PLATFORM_DIR "macos-x86_64")
    endif()
    set(ZMQ_LIB_NAME "libzmq.dylib")
elseif(UNIX)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(PLATFORM_DIR "linux-arm64")
    else()
        set(PLATFORM_DIR "linux-x64")
    endif()
    set(ZMQ_LIB_NAME "libzmq.so")
endif()

# Set library search path
set(DIST_DIR "${CMAKE_SOURCE_DIR}/../dist/${PLATFORM_DIR}")

# Find libzmq
if(WIN32)
    # Windows uses import library
    find_library(ZMQ_LIBRARY
        NAMES zmq libzmq ${ZMQ_IMPORT_LIB}
        PATHS "${DIST_DIR}"
        NO_DEFAULT_PATH
    )
    # Also need the DLL for runtime
    set(ZMQ_DLL "${DIST_DIR}/${ZMQ_LIB_NAME}")
else()
    find_library(ZMQ_LIBRARY
        NAMES zmq
        PATHS "${DIST_DIR}"
        NO_DEFAULT_PATH
    )
endif()

if(NOT ZMQ_LIBRARY)
    message(FATAL_ERROR "libzmq not found in ${DIST_DIR}. Please build libzmq first.")
endif()

message(STATUS "Found libzmq: ${ZMQ_LIBRARY}")
message(STATUS "Platform: ${PLATFORM_DIR}")

# Include directory (assumes headers are installed or available)
# For testing, we'll use system zmq.h or provide a minimal one
if(WIN32)
    find_path(ZMQ_INCLUDE_DIR zmq.h
        PATHS "${CMAKE_SOURCE_DIR}/../build/${PLATFORM_DIR}/install/include"
              "${CMAKE_SOURCE_DIR}/../build/windows-x64/install/include"
              "${DIST_DIR}/include"
    )
else()
    find_path(ZMQ_INCLUDE_DIR zmq.h
        PATHS "${CMAKE_SOURCE_DIR}/../build/${PLATFORM_DIR}/install/include"
              "${DIST_DIR}/include"
              "/usr/include"
              "/usr/local/include"
              "${CMAKE_SOURCE_DIR}/../build/libzmq/include"
    )
endif()

if(NOT ZMQ_INCLUDE_DIR)
    message(WARNING "zmq.h not found. Tests may fail to compile.")
endif()

# Test executables
add_executable(test_basic test_basic.c)
add_executable(test_curve test_curve.c)

# Link libraries
target_link_libraries(test_basic ${ZMQ_LIBRARY})
target_link_libraries(test_curve ${ZMQ_LIBRARY})

if(ZMQ_INCLUDE_DIR)
    target_include_directories(test_basic PRIVATE ${ZMQ_INCLUDE_DIR})
    target_include_directories(test_curve PRIVATE ${ZMQ_INCLUDE_DIR})
endif()

# Windows-specific: Copy all DLLs to test directory
if(WIN32)
    # Copy all DLLs from dist folder (includes libzmq.dll and VC++ runtime DLLs)
    file(GLOB DIST_DLLS "${DIST_DIR}/*.dll")
    foreach(DLL_FILE ${DIST_DLLS})
        # Copy DLLs for test_basic
        add_custom_command(TARGET test_basic POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DLL_FILE}" "$<TARGET_FILE_DIR:test_basic>"
            COMMENT "Copying ${DLL_FILE} to test_basic directory"
        )
        # Copy DLLs for test_curve (may be in same directory, but ensures both targets have DLLs)
        add_custom_command(TARGET test_curve POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DLL_FILE}" "$<TARGET_FILE_DIR:test_curve>"
            COMMENT "Copying ${DLL_FILE} to test_curve directory"
        )
    endforeach()
endif()

# Linux/macOS: Set RPATH
if(UNIX)
    set_target_properties(test_basic PROPERTIES
        BUILD_RPATH "${DIST_DIR}"
        INSTALL_RPATH "${DIST_DIR}"
    )
    set_target_properties(test_curve PROPERTIES
        BUILD_RPATH "${DIST_DIR}"
        INSTALL_RPATH "${DIST_DIR}"
    )
endif()

# Enable testing
enable_testing()
add_test(NAME BasicFunctionality COMMAND test_basic)
add_test(NAME CurveEncryption COMMAND test_curve)

# Test properties
set_tests_properties(BasicFunctionality PROPERTIES
    TIMEOUT 30
    FAIL_REGULAR_EXPRESSION "FAILED"
)

set_tests_properties(CurveEncryption PROPERTIES
    TIMEOUT 30
    FAIL_REGULAR_EXPRESSION "FAILED"
)
