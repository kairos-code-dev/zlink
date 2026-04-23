#!/usr/bin/env bash

print_completion_section() {
  local status="${1:-partial}"
  local expected_result_lines="${2:-0}"
  local actual_result_lines="${3:-0}"

  print_line ""
  print_line "## Completion"
  print_line "- status: ${status}"
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
