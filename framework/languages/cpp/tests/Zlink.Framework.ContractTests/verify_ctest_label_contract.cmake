if(NOT DEFINED ZLINK_FRAMEWORK_CPP_BUILD_DIR)
  message(FATAL_ERROR "ZLINK_FRAMEWORK_CPP_BUILD_DIR is required")
endif()
if(NOT DEFINED ZLINK_FRAMEWORK_CPP_SOURCE_DIR)
  message(FATAL_ERROR "ZLINK_FRAMEWORK_CPP_SOURCE_DIR is required")
endif()

set(required_labels
  framework-contract
  framework-unit
  framework-config
  framework-regression
  framework-host
  framework-integration
  framework-zlink
  framework-zlink-channel
  framework-zlink-spot
  framework-zlink-stream
  framework-zlink-actor-gateway
  framework-zlink-registry
  framework-observability
  timer
  framework-http
  framework-http-e2e
  framework-http-perf
  framework-package
  framework-tooling
  framework-extension
  framework-sample-smoke
  framework-sample-parity
  framework-sample-api
  framework-sample-bingo
  framework-sample-play
  framework-sample-registry
  framework-sample-session
  framework-sample-tictactoe
  http-client-contract
  http-client-unit
  http-client-e2e
  http-client-https
  http-client-regression
  connector-unit
  connector-integration
  connector-e2e
  connector-contract
  connector-protocol
  connector-transport
  connector-typed
  connector-package
  unreal-connector-contract
  unreal-connector-compile
  unreal-connector-smoke
  parity)

set(known_labels
  ${required_labels}
  ActorGateway
  DI
  actor
  async
  backpressure
  channel
  execution
  gtest
  handler
  hosted
  http
  messaging
  module
  monitoring
  registry
  reliability
  runtime
  scope
  serializer
  spot
  stream)

if(ZLINK_FRAMEWORK_CPP_EXPECT_COVERAGE_LABEL)
  list(APPEND required_labels framework-coverage)
  list(APPEND known_labels framework-coverage)
endif()

execute_process(
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${ZLINK_FRAMEWORK_CPP_BUILD_DIR}" --print-labels
  RESULT_VARIABLE print_labels_result
  OUTPUT_VARIABLE print_labels_output
  ERROR_VARIABLE print_labels_error)
if(NOT print_labels_result EQUAL 0)
  message(FATAL_ERROR "ctest label print failed: ${print_labels_error}")
endif()

string(REGEX MATCHALL
  "(^|\n)  (http-client-[A-Za-z0-9_-]+|connector-[A-Za-z0-9_-]+|unreal-connector-[A-Za-z0-9_-]+|framework-sample-[A-Za-z0-9_-]+)"
  wildcard_label_lines
  "${print_labels_output}")
foreach(label_line IN LISTS wildcard_label_lines)
  string(REGEX REPLACE "^(\\n)?  " "" wildcard_label "${label_line}")
  string(STRIP "${wildcard_label}" wildcard_label)
  list(FIND required_labels "${wildcard_label}" required_index)
  if(required_index EQUAL -1)
    message(FATAL_ERROR
      "CTest wildcard-prefix label is not covered by required_labels: ${wildcard_label}")
  endif()
endforeach()

string(REGEX MATCHALL
  "(^|\n)  [A-Za-z0-9_-]+"
  label_lines
  "${print_labels_output}")
foreach(label_line IN LISTS label_lines)
  string(REGEX REPLACE "^(\\n)?  " "" actual_label "${label_line}")
  string(STRIP "${actual_label}" actual_label)
  list(FIND known_labels "${actual_label}" known_index)
  if(known_index EQUAL -1)
    message(FATAL_ERROR
      "CTest label is not covered by known label taxonomy: ${actual_label}")
  endif()
endforeach()

foreach(label IN LISTS required_labels)
  execute_process(
    COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${ZLINK_FRAMEWORK_CPP_BUILD_DIR}" -N -L "${label}"
    RESULT_VARIABLE ctest_result
    OUTPUT_VARIABLE ctest_output
    ERROR_VARIABLE ctest_error)
  if(NOT ctest_result EQUAL 0)
    message(FATAL_ERROR "ctest label scan failed for ${label}: ${ctest_error}")
  endif()
  if(NOT ctest_output MATCHES "Total Tests: *([0-9]+)")
    message(FATAL_ERROR "ctest label scan did not report a test count for ${label}")
  endif()
  set(test_count "${CMAKE_MATCH_1}")
  if(test_count LESS 1)
    message(FATAL_ERROR "CTest label ${label} selects no tests")
  endif()
endforeach()

execute_process(
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${ZLINK_FRAMEWORK_CPP_BUILD_DIR}" -N -L http-client-https
  RESULT_VARIABLE http_client_https_result
  OUTPUT_VARIABLE http_client_https_output
  ERROR_VARIABLE http_client_https_error)
if(NOT http_client_https_result EQUAL 0)
  message(FATAL_ERROR
    "ctest label scan failed for http-client-https: ${http_client_https_error}")
endif()
if(NOT http_client_https_output MATCHES "test_cpp_http_client")
  message(FATAL_ERROR
    "http-client-https must select the HTTP client HTTPS regression test")
endif()
if(http_client_https_output MATCHES "test_cpp_framework_contract_headers")
  message(FATAL_ERROR
    "http-client-https must not be satisfied by public header compile smoke")
endif()

get_filename_component(zlink_repo_root
  "${ZLINK_FRAMEWORK_CPP_SOURCE_DIR}/../../.." ABSOLUTE)
set(implementation_plan
  "${ZLINK_FRAMEWORK_CPP_SOURCE_DIR}/doc/draft/cpp-framework-implementation-plan.ko.md")
file(STRINGS "${implementation_plan}" plan_ctest_commands
  REGEX "^ctest --test-dir ")
foreach(plan_command IN LISTS plan_ctest_commands)
  if(NOT plan_command MATCHES "^ctest --test-dir ([^ ]+)(.*)$")
    message(FATAL_ERROR
      "implementation plan CTest command has unsupported form: ${plan_command}")
  endif()
  set(plan_test_dir_rel "${CMAKE_MATCH_1}")
  set(plan_command_tail "${CMAKE_MATCH_2}")

  set(plan_test_dir "${zlink_repo_root}/${plan_test_dir_rel}")
  if(plan_test_dir_rel STREQUAL "framework/languages/cpp/build")
    set(plan_test_dir "${ZLINK_FRAMEWORK_CPP_BUILD_DIR}")
  elseif(plan_test_dir_rel STREQUAL "framework/languages/cpp/build-coverage")
    if(ZLINK_FRAMEWORK_CPP_EXPECT_COVERAGE_LABEL)
      set(plan_test_dir "${ZLINK_FRAMEWORK_CPP_BUILD_DIR}")
    elseif(NOT EXISTS "${plan_test_dir}")
      continue()
    endif()
  elseif(NOT EXISTS "${plan_test_dir}")
    continue()
  endif()

  set(plan_scan_command
    "${CMAKE_CTEST_COMMAND}" --test-dir "${plan_test_dir}" -N)
  if(plan_command_tail MATCHES " -L ([^ ]+)")
    list(APPEND plan_scan_command -L "${CMAKE_MATCH_1}")
  endif()
  if(plan_command_tail MATCHES " -R ([^ ]+)")
    list(APPEND plan_scan_command -R "${CMAKE_MATCH_1}")
  endif()

  execute_process(
    COMMAND ${plan_scan_command}
    RESULT_VARIABLE plan_scan_result
    OUTPUT_VARIABLE plan_scan_output
    ERROR_VARIABLE plan_scan_error)
  if(NOT plan_scan_result EQUAL 0)
    message(FATAL_ERROR
      "implementation plan CTest command scan failed: ${plan_command}\n${plan_scan_error}")
  endif()
  if(NOT plan_scan_output MATCHES "Total Tests: *([0-9]+)")
    message(FATAL_ERROR
      "implementation plan CTest command did not report a test count: ${plan_command}")
  endif()
  set(plan_test_count "${CMAKE_MATCH_1}")
  if(plan_test_count LESS 1)
    message(FATAL_ERROR
      "implementation plan CTest command selects no tests: ${plan_command}")
  endif()
endforeach()
