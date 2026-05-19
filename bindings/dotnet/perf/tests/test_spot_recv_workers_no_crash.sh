#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOTNET_PERF_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
RUNNER="${DOTNET_PERF_DIR}/run_benchmarks_multi.sh"

workers="${PERF_DOTNET_SPOT_RECV_WORKERS_REGRESSION:-8}"
sizes="${PERF_DOTNET_SPOT_RECV_WORKERS_SIZES:-64,256,1024,65536,131072,262144}"
duration="${PERF_DOTNET_SPOT_RECV_WORKERS_DURATION:-1}"
tag="${PERF_DOTNET_SPOT_RECV_WORKERS_TAG:-spot_recv_workers_no_crash}"
log_file="$(mktemp -t zlink-dotnet-spot-workers.XXXXXX.log)"
trap 'rm -f "${log_file}"' EXIT

set +e
PERF_FAIL_FAST=1 \
PERF_DOTNET_SPOT_RECV_WORKERS="${workers}" \
"${RUNNER}" \
  --pattern MULTI_SPOT \
  --transports tcp \
  --msg-sizes "${sizes}" \
  --duration "${duration}" \
  --results-tag "${tag}" \
  >"${log_file}" 2>&1
status=$?
set -e

if rg -n "Segmentation fault|core dumped|Fatal error|Aborted" "${log_file}" >&2; then
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
