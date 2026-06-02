if(NOT DEFINED ZLINK_FRAMEWORK_CPP_BUILD_DIR)
  message(FATAL_ERROR "ZLINK_FRAMEWORK_CPP_BUILD_DIR is required")
endif()
if(NOT DEFINED ZLINK_FRAMEWORK_CPP_SOURCE_DIR)
  message(FATAL_ERROR "ZLINK_FRAMEWORK_CPP_SOURCE_DIR is required")
endif()
if(NOT DEFINED ZLINK_FRAMEWORK_CPP_COVERAGE_THRESHOLD)
  set(ZLINK_FRAMEWORK_CPP_COVERAGE_THRESHOLD 70)
endif()
if(NOT DEFINED ZLINK_GCOV_EXECUTABLE)
  set(ZLINK_GCOV_EXECUTABLE gcov)
endif()

set(gcov_work_dir "${ZLINK_FRAMEWORK_CPP_BUILD_DIR}/coverage-gcov")
file(REMOVE_RECURSE "${gcov_work_dir}")
file(MAKE_DIRECTORY "${gcov_work_dir}")

set(runtime_target_dirs
  zlink_framework
  zlink_http_client
  zlink_stream_connector
  zlink_unreal_stream_connector)

set(gcda_files)
foreach(target_name IN LISTS runtime_target_dirs)
  file(GLOB_RECURSE target_gcda
    "${ZLINK_FRAMEWORK_CPP_BUILD_DIR}/CMakeFiles/${target_name}.dir/*.gcda")
  list(APPEND gcda_files ${target_gcda})
endforeach()

if(NOT gcda_files)
  message(FATAL_ERROR
    "No coverage data found. Configure with -DZLINK_FRAMEWORK_CPP_ENABLE_COVERAGE=ON and run tests first.")
endif()

foreach(gcda IN LISTS gcda_files)
  get_filename_component(gcda_dir "${gcda}" DIRECTORY)
  execute_process(
    COMMAND "${ZLINK_GCOV_EXECUTABLE}" -o "${gcda_dir}" "${gcda}"
    WORKING_DIRECTORY "${gcov_work_dir}"
    RESULT_VARIABLE gcov_result
    OUTPUT_QUIET
    ERROR_VARIABLE gcov_error)
  if(NOT gcov_result EQUAL 0)
    message(FATAL_ERROR "gcov failed for ${gcda}: ${gcov_error}")
  endif()
endforeach()

file(GLOB gcov_reports "${gcov_work_dir}/*.gcov")
set(covered_lines 0)
set(total_lines 0)
set(source_files_seen 0)

foreach(report IN LISTS gcov_reports)
  file(READ "${report}" report_text)
  string(REPLACE "\n" ";" report_lines "${report_text}")
  set(source_path "")
  foreach(line IN LISTS report_lines)
    if(line MATCHES "^ *-: *0:Source:(.*)$")
      set(source_path "${CMAKE_MATCH_1}")
      file(REAL_PATH "${source_path}" source_path
        BASE_DIRECTORY "${ZLINK_FRAMEWORK_CPP_BUILD_DIR}")
      break()
    endif()
  endforeach()

  if(NOT source_path MATCHES
      "^${ZLINK_FRAMEWORK_CPP_SOURCE_DIR}/(framework/src|http-client/src|connector/src|unreal-connector/Source)/.*\\.(cpp|hpp)$")
    continue()
  endif()

  math(EXPR source_files_seen "${source_files_seen} + 1")
  foreach(line IN LISTS report_lines)
    if(NOT line MATCHES "^ *([^:]+): *[0-9]+:")
      continue()
    endif()
    set(count "${CMAKE_MATCH_1}")
    string(STRIP "${count}" count)
    if(count STREQUAL "-")
      continue()
    endif()
    math(EXPR total_lines "${total_lines} + 1")
    if(NOT count MATCHES "^#+$" AND NOT count STREQUAL "0")
      math(EXPR covered_lines "${covered_lines} + 1")
    endif()
  endforeach()
endforeach()

if(total_lines EQUAL 0)
  message(FATAL_ERROR "No runtime source lines were found in coverage reports.")
endif()

math(EXPR coverage_times_100
  "(${covered_lines} * 10000 + (${total_lines} / 2)) / ${total_lines}")
math(EXPR coverage_integer "${coverage_times_100} / 100")
math(EXPR coverage_fraction "${coverage_times_100} % 100")

message(STATUS
  "C++ framework runtime line coverage: ${coverage_integer}.${coverage_fraction}% (${covered_lines}/${total_lines}, ${source_files_seen} files)")

math(EXPR threshold_times_100
  "${ZLINK_FRAMEWORK_CPP_COVERAGE_THRESHOLD} * 100")
if(coverage_times_100 LESS threshold_times_100)
  message(FATAL_ERROR
    "C++ framework runtime line coverage ${coverage_integer}.${coverage_fraction}% is below ${ZLINK_FRAMEWORK_CPP_COVERAGE_THRESHOLD}%")
endif()
