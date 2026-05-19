#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOTNET_PERF_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
RUNNER="${DOTNET_PERF_DIR}/run_benchmarks_multi.sh"

sizes="${PERF_DOTNET_SPOT_REQREP_SHUTDOWN_SIZES:-64,256}"
duration="${PERF_DOTNET_SPOT_REQREP_SHUTDOWN_DURATION:-1}"
tag="${PERF_DOTNET_SPOT_REQREP_SHUTDOWN_TAG:-spot_reqrep_small_no_hidden_shutdown}"
log_file="$(mktemp -t zlink-dotnet-spot-reqrep-shutdown.XXXXXX.log)"
trap 'rm -f "${log_file}"' EXIT

set +e
PERF_FAIL_FAST=1 \
"${RUNNER}" \
  --pattern MULTI_SPOT_REQREP \
  --transports tcp \
  --msg-sizes "${sizes}" \
  --duration "${duration}" \
  --results-tag "${tag}" \
  >"${log_file}" 2>&1
status=$?
set -e

if rg -n "did not exit|Segmentation fault|core dumped|Fatal error|Aborted|Assertion failed|exited with status" "${log_file}" >&2; then
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
