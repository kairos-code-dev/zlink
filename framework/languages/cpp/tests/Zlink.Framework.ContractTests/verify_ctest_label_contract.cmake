if(NOT DEFINED ZLINK_FRAMEWORK_CPP_BUILD_DIR)
  message(FATAL_ERROR "ZLINK_FRAMEWORK_CPP_BUILD_DIR is required")
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
  framework-package
  framework-extension
  framework-sample-smoke
  framework-sample-parity
  framework-sample-e2e
  framework-sample-log
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
  unreal-connector-contract
  unreal-connector-compile
  unreal-connector-smoke
  parity)

if(ZLINK_FRAMEWORK_CPP_EXPECT_COVERAGE_LABEL)
  list(APPEND required_labels framework-coverage)
endif()

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
