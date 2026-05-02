# CMake build script for Zlink unit tests
cmake_minimum_required(VERSION 3.10)

set(unittests
    unittest_ypipe
    unittest_poller
    unittest_service_mode_policy
    unittest_mtrie
    unittest_ip_resolver
    unittest_radix_tree
    unittest_zmp_decoder
    unittest_raw_decoder
    unittest_msg_view
    unittest_ctx_runtime
    unittest_service_runtime_base
    unittest_discovery_service_state
    unittest_spot_data_plane_protocol
    unittest_spot_data_plane_budget
    unittest_spot_runtime_control_state
    unittest_socket_runtime
    unittest_spot_subject_access
    unittest_registry_access
    unittest_typed_option
    unittest_result_enum_mapping)

# add location of platform.hpp for Windows builds
if(WIN32)
  add_definitions(-DZLINK_CUSTOM_PLATFORM_HPP)
  add_definitions(-D_WINSOCK_DEPRECATED_NO_WARNINGS)
  # Same name on 64bit systems
  link_libraries(ws2_32.lib)
endif()

include_directories("${ZLINK_CORE_SOURCE_DIR}/include"
                    "${ZLINK_CORE_SOURCE_DIR}/src"
                    "${CMAKE_BINARY_DIR}")
include_directories("${ZLINK_CORE_SOURCE_DIR}/external/unity")
# Add Boost headers for ASIO poller
include_directories("${BUNDLED_BOOST_INCLUDE_DIR}")

foreach(test ${unittests})
  # target_sources not supported before CMake 3.1
  add_executable(${test} ${test}.cpp "unittest_resolver_common.hpp")

  # per-test directories not generated on OS X / Darwin
  if(NOT ${CMAKE_CXX_COMPILER_ID} MATCHES "Clang.*")
    link_directories(${test} PRIVATE "${Zlink_SOURCE_DIR}/../lib")
  endif()

  target_link_libraries(${test} testutil-static)

  if(RT_LIBRARY)
    target_link_libraries(${test} ${RT_LIBRARY})
  endif()

  if(CMAKE_SYSTEM_NAME MATCHES "QNX")
    target_link_libraries(${test} socket)
    target_link_libraries(${test} m)
  endif()

  if(WIN32)
    add_test(
      NAME ${test}
      WORKING_DIRECTORY ${LIBRARY_OUTPUT_PATH}
      COMMAND ${test})
  else()
    add_test(NAME ${test} COMMAND ${test})
  endif()

  set_tests_properties(${test} PROPERTIES TIMEOUT 10)
  set_tests_properties(${test} PROPERTIES LABELS "unittest;parallel-safe")

  if(${test} STREQUAL "unittest_service_mode_policy")
    set_tests_properties(${test} PROPERTIES
      TIMEOUT 30
      RUN_SERIAL TRUE
      LABELS "unittest;serial")
  endif()

  # TODO prevent libzlink (non-static) being in the list of link libraries at all
  get_target_property(LIBS ${test} LINK_LIBRARIES)
  list(REMOVE_ITEM LIBS libzlink)
  set_target_properties(${test} PROPERTIES LINK_LIBRARIES "${LIBS}")
endforeach()

# Check whether all tests in the current folder are present TODO duplicated with tests/CMakeLists.txt, define as a
# function?
file(READ "${CMAKE_CURRENT_LIST_FILE}" CURRENT_LIST_FILE_CONTENT)
file(GLOB ALL_TEST_SOURCES "unittest_*.cpp")
foreach(TEST_SOURCE ${ALL_TEST_SOURCES})
  get_filename_component(TESTNAME "${TEST_SOURCE}" NAME_WE)
  string(REGEX MATCH "${TESTNAME}" MATCH_TESTNAME "${CURRENT_LIST_FILE_CONTENT}")
  if(NOT MATCH_TESTNAME)
    message(AUTHOR_WARNING "Test '${TESTNAME}' is not known to CTest.")
  endif()
endforeach()
