#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOTNET_PERF_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
RUNNER="${DOTNET_PERF_DIR}/run_benchmarks_multi.sh"

sizes="${PERF_DOTNET_SPOT_SENDSEND_TLS_RESULT_SIZES:-65536}"
duration="${PERF_DOTNET_SPOT_SENDSEND_TLS_RESULT_DURATION:-1}"
tag="${PERF_DOTNET_SPOT_SENDSEND_TLS_RESULT_TAG:-spot_sendsend_tls_large_produces_result}"
log_file="$(mktemp -t zlink-dotnet-spot-sendsend-tls-result.XXXXXX.log)"
trap 'rm -f "${log_file}"' EXIT

set +e
PERF_FAIL_FAST=1 \
"${RUNNER}" \
  --pattern MULTI_SPOT_SENDSEND \
  --transports tls \
  --msg-sizes "${sizes}" \
  --duration "${duration}" \
  --results-tag "${tag}" \
  >"${log_file}" 2>&1
status=$?
set -e

if rg -n "result_timeout|missing_required_result_lines|FAIL|status: partial|status: failed|did not exit|exited with status" "${log_file}" >&2; then
  cat "${log_file}" >&2
  exit 1
fi

if [[ "${status}" -ne 0 ]]; then
  cat "${log_file}" >&2
  exit "${status}"
fi

if ! rg -q "RESULT,current,MULTI_SPOT_SENDSEND,tls,65536,throughput," "${log_file}"; then
  cat "${log_file}" >&2
  exit 1
fi

if ! rg -q "status: complete" "${log_file}"; then
  cat "${log_file}" >&2
  exit 1
fi
