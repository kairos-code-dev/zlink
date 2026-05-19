#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
C_PERF_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
RUNNER="${C_PERF_DIR}/run_benchmarks_multi.sh"

tag="${PERF_C_SPOT_SENDSEND_WS64_TAG:-spot_sendsend_ws64_no_timeout}"
duration="${PERF_C_SPOT_SENDSEND_WS64_DURATION:-1}"
timeout_seconds="${PERF_C_SPOT_SENDSEND_WS64_TIMEOUT_SECONDS:-30}"
log_file="$(mktemp -t zlink-c-spot-sendsend-ws64.XXXXXX.log)"
trap 'rm -f "${log_file}"' EXIT

set +e
PERF_FAIL_FAST=1 \
PERF_TIMEOUT_SECONDS="${timeout_seconds}" \
"${RUNNER}" \
  --pattern MULTI_SPOT_SENDSEND \
  --transports ws \
  --msg-sizes 64 \
  --duration "${duration}" \
  --results-tag "${tag}" \
  >"${log_file}" 2>&1
status=$?
set -e

if rg -n "FAIL|: timeout|did not exit|Segmentation fault|core dumped|Fatal error|Aborted|exited with status" "${log_file}" >&2; then
  cat "${log_file}" >&2
  exit 1
fi

if [[ "${status}" -ne 0 ]]; then
  cat "${log_file}" >&2
  exit "${status}"
fi

if ! rg -q "status: complete" "${log_file}"; then
  cat "${log_file}" >&2
  exit 1
fi
