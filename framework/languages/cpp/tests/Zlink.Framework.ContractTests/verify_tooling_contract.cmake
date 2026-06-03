if(NOT DEFINED ZLINK_FRAMEWORK_CPP_SOURCE_DIR)
  message(FATAL_ERROR "ZLINK_FRAMEWORK_CPP_SOURCE_DIR is required")
endif()

set(presets_file "${ZLINK_FRAMEWORK_CPP_SOURCE_DIR}/CMakePresets.json")
set(vcpkg_file "${ZLINK_FRAMEWORK_CPP_SOURCE_DIR}/vcpkg.json")

if(NOT EXISTS "${presets_file}")
  message(FATAL_ERROR "CMakePresets.json is required")
endif()
if(NOT EXISTS "${vcpkg_file}")
  message(FATAL_ERROR "vcpkg.json is required")
endif()

file(READ "${presets_file}" presets_text)
file(READ "${vcpkg_file}" vcpkg_text)

foreach(required
    "\"linux-ninja-debug\""
    "\"linux-ninja-release\""
    "\"linux-ninja-vcpkg-debug\""
    "\"macos-ninja-debug\""
    "\"windows-msvc-debug\""
    "\"windows-msvc-release\""
    "\"windows-msvc-vcpkg-debug\""
    "\"Visual Studio 17 2022\""
    "\"CMAKE_CXX_STANDARD\": \"20\""
    "\"CMAKE_EXPORT_COMPILE_COMMANDS\": \"ON\""
    "\"ZLINK_FRAMEWORK_CPP_BUILD_TESTS\": \"ON\""
    "\"ZLINK_FRAMEWORK_CPP_BUILD_SAMPLES\": \"ON\"")
  if(NOT presets_text MATCHES "${required}")
    message(FATAL_ERROR "CMakePresets.json is missing ${required}")
  endif()
endforeach()

foreach(required
    "\"boost-asio\""
    "\"gtest\""
    "\"lz4\""
    "\"nlohmann-json\""
    "\"openssl\"")
  if(NOT vcpkg_text MATCHES "${required}")
    message(FATAL_ERROR "vcpkg.json is missing ${required}")
  endif()
endforeach()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --list-presets=all
  WORKING_DIRECTORY "${ZLINK_FRAMEWORK_CPP_SOURCE_DIR}"
  RESULT_VARIABLE list_result
  OUTPUT_VARIABLE list_output
  ERROR_VARIABLE list_error)
if(NOT list_result EQUAL 0)
  message(FATAL_ERROR "cmake --list-presets=all failed: ${list_error}")
endif()

foreach(required
    "linux-ninja-debug"
    "linux-ninja-release"
    "linux-ninja-vcpkg-debug"
    "macos-ninja-debug"
    "windows-msvc-debug"
    "windows-msvc-release"
    "windows-msvc-vcpkg-debug")
  if(NOT list_output MATCHES "${required}")
    message(FATAL_ERROR "cmake --list-presets=all did not list ${required}")
  endif()
endforeach()
