if(NOT DEFINED ZLINK_FRAMEWORK_CPP_SOURCE_DIR)
  message(FATAL_ERROR "ZLINK_FRAMEWORK_CPP_SOURCE_DIR is required")
endif()

set(embedded_http_server_spec
  "${ZLINK_FRAMEWORK_CPP_SOURCE_DIR}/../../doc/framework/common/spec/languages/cpp/61-embedded-http-server.ko.md")

if(NOT EXISTS "${embedded_http_server_spec}")
  message(FATAL_ERROR "HTTP perf policy document is missing: ${embedded_http_server_spec}")
endif()

file(READ "${embedded_http_server_spec}" policy_text)

foreach(required_text IN ITEMS
    "framework-http-perf"
    "Drogon"
    "Oat++"
    "같은 load generator"
    "처리량 하락이 10%"
    "하락이 15%"
    "p95 latency"
    "bounded worker pool"
    "route table"
    "connection buffer")
  string(FIND "${policy_text}" "${required_text}" required_pos)
  if(required_pos EQUAL -1)
    message(FATAL_ERROR "HTTP perf policy lacks required criterion: ${required_text}")
  endif()
endforeach()

foreach(forbidden_text IN ITEMS
    "zlink::http_client"
    "같은 load generator"
    "구현 병목이 server 수치에")
  string(FIND "${policy_text}" "${forbidden_text}" required_pos)
  if(required_pos EQUAL -1)
    message(FATAL_ERROR "HTTP perf spec must keep server measurement independent from the client implementation: ${forbidden_text}")
  endif()
endforeach()
