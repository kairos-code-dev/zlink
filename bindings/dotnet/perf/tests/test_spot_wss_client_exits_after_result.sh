#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOTNET_PERF_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
RUNNER="${DOTNET_PERF_DIR}/run_benchmarks_multi.sh"

size="${PERF_DOTNET_SPOT_WSS_EXIT_SIZE:-64}"
duration="${PERF_DOTNET_SPOT_WSS_EXIT_DURATION:-1}"
tag="${PERF_DOTNET_SPOT_WSS_EXIT_TAG:-spot_wss_client_exits_after_result}"
log_file="$(mktemp -t zlink-dotnet-spot-wss-exit.XXXXXX.log)"
trap 'rm -f "${log_file}"' EXIT

set +e
PERF_FAIL_FAST=1 \
"${RUNNER}" \
  --pattern MULTI_SPOT \
  --transports wss \
  --msg-sizes "${size}" \
  --duration "${duration}" \
  --results-tag "${tag}" \
  >"${log_file}" 2>&1
status=$?
set -e

if rg -n "client did not exit|server did not exit|FAIL|status: partial|status: failed" "${log_file}" >&2; then
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
