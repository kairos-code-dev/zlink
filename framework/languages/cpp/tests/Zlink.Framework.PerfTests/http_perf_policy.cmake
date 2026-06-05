if(NOT DEFINED ZLINK_FRAMEWORK_CPP_SOURCE_DIR)
  message(FATAL_ERROR "ZLINK_FRAMEWORK_CPP_SOURCE_DIR is required")
endif()

set(implementation_plan
  "${ZLINK_FRAMEWORK_CPP_SOURCE_DIR}/doc/draft/cpp-framework-implementation-plan.ko.md")
set(embedded_http_server_plan
  "${ZLINK_FRAMEWORK_CPP_SOURCE_DIR}/doc/draft/cpp-embedded-http-server.ko.md")

foreach(path IN ITEMS
    "${implementation_plan}"
    "${embedded_http_server_plan}")
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "HTTP perf policy document is missing: ${path}")
  endif()
endforeach()

file(READ "${implementation_plan}" implementation_plan_text)
file(READ "${embedded_http_server_plan}" embedded_plan_text)
set(combined_text "${implementation_plan_text}\n${embedded_plan_text}")

foreach(required_text IN ITEMS
    "framework-http-perf"
    "Drogon"
    "Oat++"
    "같은 load generator"
    "처리량 하락 10% 이내"
    "처리량 하락 15% 이내"
    "p95 latency"
    "bounded I/O worker pool"
    "route table"
    "connection buffer")
  string(FIND "${combined_text}" "${required_text}" required_pos)
  if(required_pos EQUAL -1)
    message(FATAL_ERROR "HTTP perf policy lacks required criterion: ${required_text}")
  endif()
endforeach()

foreach(forbidden_text IN ITEMS
    "zlink::http_client"
    "같은 load generator"
    "client 구현 비용이 server 수치에 섞이지 않게")
  string(FIND "${implementation_plan_text}" "${forbidden_text}" required_pos)
  if(required_pos EQUAL -1)
    message(FATAL_ERROR "Goal 19 must keep server perf independent from zlink::http_client: ${forbidden_text}")
  endif()
endforeach()
