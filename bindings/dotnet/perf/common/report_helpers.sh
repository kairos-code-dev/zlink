#!/usr/bin/env bash

print_completion_section() {
  local status="${1:-partial}"
  local expected_result_lines="${2:-0}"
  local actual_result_lines="${3:-0}"
  local success_count="${4:-}"
  local unsupported_count="${5:-}"
  local skip_count="${6:-}"
  local fail_count="${7:-}"
  local fail_fast_stopped="${8:-}"

  print_line ""
  print_line "## Completion"
  if [[ -n "${success_count}" ]]; then
    print_line "- success: ${success_count}"
    print_line "- unsupported: ${unsupported_count:-0}"
    print_line "- skip: ${skip_count:-0}"
    print_line "- fail: ${fail_count:-0}"
  fi
  print_line "- status: ${status}"
  if [[ -n "${fail_fast_stopped}" ]]; then
    print_line "- fail_fast_stopped: ${fail_fast_stopped}"
  fi
  print_line "- expected_result_lines: ${expected_result_lines}"
  print_line "- actual_result_lines: ${actual_result_lines}"
}

print_failures_section() {
  local failures_file="${1:-}"

  print_line ""
  print_line "## Failures"
  if [[ -n "${failures_file}" && -s "${failures_file}" ]]; then
    while IFS=',' read -r pattern transport size run_index reason; do
      print_line "- pattern=${pattern} transport=${transport} size=${size} run=${run_index} reason=${reason}"
    done < "${failures_file}"
  fi
}
