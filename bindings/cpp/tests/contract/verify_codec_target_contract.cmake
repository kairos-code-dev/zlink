if(NOT DEFINED ZLINK_CPP_BUILD_DIR)
  message(FATAL_ERROR "ZLINK_CPP_BUILD_DIR is required")
endif()

set(report_file "${ZLINK_CPP_BUILD_DIR}/codec-target-contract.txt")
if(NOT EXISTS "${report_file}")
  message(FATAL_ERROR "codec target contract report is missing")
endif()

file(READ "${report_file}" report_text)

foreach(forbidden nlohmann_json msgpack protobuf)
  if(report_text MATCHES "base_links=[^\\n]*${forbidden}")
    message(FATAL_ERROR "base zlink_cpp target links codec dependency ${forbidden}")
  endif()
endforeach()

if(report_text MATCHES "codec_target_without_base=")
  message(FATAL_ERROR "a codec target does not link the base zlink_cpp target")
endif()
