#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${ROOT_DIR}/../../../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${REPO_ROOT}/bindings/c/build}"
RUN_STAMP="${RUN_STAMP:-$(date +%Y%m%d_%H%M%S)}"
OUTPUT="${OUTPUT:-${ROOT_DIR}/log/with_grpc_c_${RUN_STAMP}}"
REPORT_FILE="${REPORT_FILE:-with_grpc_c_${RUN_STAMP}.txt}"
REPORT_PATH="${OUTPUT}/${REPORT_FILE}"
PAYLOAD_SIZES="${PAYLOAD_SIZES:-1024,4096}"
DURATION_SECONDS="${DURATION_SECONDS:-3}"
WINDOW_SIZE="${WINDOW_SIZE:-100}"
MAX_OUTSTANDING="${MAX_OUTSTANDING:-4096}"
DRAIN_TIMEOUT_MS="${DRAIN_TIMEOUT_MS:-5000}"

cmake -S "${REPO_ROOT}/bindings/c" -B "${BUILD_DIR}" \
  -DZLINK_C_BUILD_BENCHES=ON \
  -DZLINK_C_BUILD_BENCH_GRPC_COMPARE=ON
cmake --build "${BUILD_DIR}" --target \
  bench_c_with_grpc_zlink_server \
  bench_c_with_grpc_zlink_client \
  bench_c_with_grpc_grpc_server \
  bench_c_with_grpc_grpc_client \
  -j"$(nproc)"

mkdir -p "${OUTPUT}"
: >"${REPORT_PATH}"

zlink_server="${BUILD_DIR}/bench/with_grpc/bench_c_with_grpc_zlink_server"
zlink_client="${BUILD_DIR}/bench/with_grpc/bench_c_with_grpc_zlink_client"
grpc_server="${BUILD_DIR}/bench/with_grpc/bench_c_with_grpc_grpc_server"
grpc_client="${BUILD_DIR}/bench/with_grpc/bench_c_with_grpc_grpc_client"

export PAYLOAD_SIZES
export DURATION_SECONDS
export WINDOW_SIZE
export MAX_OUTSTANDING
export DRAIN_TIMEOUT_MS

setsid "${zlink_server}" >"${OUTPUT}/zlink-server.log" 2>&1 &
zlink_pid=$!
setsid "${grpc_server}" >"${OUTPUT}/grpc-server.log" 2>&1 &
grpc_pid=$!

cleanup() {
  status=$?
  set +e
  kill -TERM -- "-${zlink_pid}" "-${grpc_pid}" >/dev/null 2>&1 || true
  sleep 1
  kill -KILL -- "-${zlink_pid}" "-${grpc_pid}" >/dev/null 2>&1 || true
  wait "${zlink_pid}" "${grpc_pid}" >/dev/null 2>&1 || true
  exit "${status}"
}
trap cleanup EXIT

sleep 1
{
  echo "C zlink API vs gRPC local bench"
  echo
  echo "Effective Options:"
  echo "  generated_local: $(date '+%Y-%m-%dT%H:%M:%S%z')"
  echo "  payload_sizes: ${PAYLOAD_SIZES}"
  echo "  duration_seconds: ${DURATION_SECONDS}"
  echo "  window_size: ${WINDOW_SIZE}"
  echo "  max_outstanding: ${MAX_OUTSTANDING}"
  echo "  drain_timeout_ms: ${DRAIN_TIMEOUT_MS}"
  echo
  printf '| %-28s | %8s | %21s | %15s | %12s | %11s | %11s | %8s | %8s | %8s | %8s | %10s | %10s | %8s | %8s | %8s | %12s |\n' \
    "Scenario" "Size" "Throughput" "Bandwidth" "Lat.Mean(ms)" "Lat.P95(ms)" "Lat.P99(ms)" \
    "C.CPU%" "C.Mem MB" "S.CPU%" "S.Mem MB" "Submitted" "Completed" "Errors" "Blocked" \
    "MaxOut" "SubmitMs"
  printf '|%-30s|%-10s|%-23s|%-17s|%-14s|%-13s|%-13s|%-10s|%-10s|%-10s|%-10s|%-12s|%-12s|%-10s|%-10s|%-10s|%-14s|\n' \
    "------------------------------" "----------" "-----------------------" "-----------------" \
    "--------------" "-------------" "-------------" "----------" "----------" "----------" "----------" \
    "------------" "------------" "----------" "----------" "----------" "--------------"
  SERVER_PID="${grpc_pid}" "${grpc_client}"
  SERVER_PID="${zlink_pid}" "${zlink_client}"
} | tee "${REPORT_PATH}"

echo "[bench] report: ${REPORT_PATH}" >&2
