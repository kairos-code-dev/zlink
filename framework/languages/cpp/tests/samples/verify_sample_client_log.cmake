if(NOT DEFINED SAMPLE_EXECUTABLE)
  message(FATAL_ERROR "SAMPLE_EXECUTABLE is required")
endif()

if(NOT DEFINED SAMPLE_LOG)
  message(FATAL_ERROR "SAMPLE_LOG is required")
endif()

if(NOT DEFINED EXPECTED_CONTAINS)
  set(EXPECTED_CONTAINS "")
endif()

if(NOT DEFINED MIN_RECV)
  set(MIN_RECV 0)
endif()

if(NOT DEFINED MIN_REPLY)
  set(MIN_REPLY 0)
endif()

if(NOT DEFINED MIN_PUSH)
  set(MIN_PUSH 0)
endif()

get_filename_component(log_dir "${SAMPLE_LOG}" DIRECTORY)
file(MAKE_DIRECTORY "${log_dir}")
file(REMOVE "${SAMPLE_LOG}")

execute_process(
  COMMAND "${SAMPLE_EXECUTABLE}"
  WORKING_DIRECTORY "${log_dir}"
  RESULT_VARIABLE sample_result
  OUTPUT_VARIABLE sample_stdout
  ERROR_VARIABLE sample_stderr)

if(NOT sample_result EQUAL 0)
  message(STATUS "sample stdout:\n${sample_stdout}")
  message(STATUS "sample stderr:\n${sample_stderr}")
  message(FATAL_ERROR "sample executable failed with ${sample_result}")
endif()

if(NOT EXISTS "${SAMPLE_LOG}")
  message(FATAL_ERROR "sample server log was not created: ${SAMPLE_LOG}")
endif()

file(READ "${SAMPLE_LOG}" log_content)

string(REPLACE "|" ";" expected_items "${EXPECTED_CONTAINS}")
foreach(expected IN LISTS expected_items)
  if(expected STREQUAL "")
    continue()
  endif()
  string(FIND "${log_content}" "${expected}" found_at)
  if(found_at LESS 0)
    message(STATUS "sample server log:\n${log_content}")
    message(FATAL_ERROR "sample server log does not contain: ${expected}")
  endif()
endforeach()

string(REGEX MATCHALL "\nrecv " recv_lines "\n${log_content}")
list(LENGTH recv_lines recv_count)
if(recv_count LESS MIN_RECV)
  message(STATUS "sample server log:\n${log_content}")
  message(FATAL_ERROR "expected at least ${MIN_RECV} recv lines, got ${recv_count}")
endif()

string(REGEX MATCHALL "\nreply " reply_lines "\n${log_content}")
list(LENGTH reply_lines reply_count)
if(reply_count LESS MIN_REPLY)
  message(STATUS "sample server log:\n${log_content}")
  message(FATAL_ERROR "expected at least ${MIN_REPLY} reply lines, got ${reply_count}")
endif()

string(REGEX MATCHALL "\npush " push_lines "\n${log_content}")
list(LENGTH push_lines push_count)
if(push_count LESS MIN_PUSH)
  message(STATUS "sample server log:\n${log_content}")
  message(FATAL_ERROR "expected at least ${MIN_PUSH} push lines, got ${push_count}")
endif()
