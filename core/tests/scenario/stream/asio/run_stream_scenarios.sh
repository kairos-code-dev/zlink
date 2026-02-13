#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../../../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/core/build"
BIN="${BUILD_DIR}/bin/test_scenario_stream_asio"

TRANSPORT="${TRANSPORT:-tcp}"
CCU="${CCU:-10000}"
SIZE="${SIZE:-1024}"
INFLIGHT="${INFLIGHT:-30}"
WARMUP="${WARMUP:-3}"
MEASURE="${MEASURE:-10}"
DRAIN_TIMEOUT="${DRAIN_TIMEOUT:-10}"
CONNECT_CONCURRENCY="${CONNECT_CONCURRENCY:-256}"
CONNECT_TIMEOUT="${CONNECT_TIMEOUT:-10}"
BACKLOG="${BACKLOG:-32768}"
SNDBUF="${SNDBUF:-262144}"
RCVBUF="${RCVBUF:-262144}"
NO_DELAY="${NO_DELAY:-1}"
IO_THREADS="${IO_THREADS:-1}"
SEND_BATCH="${SEND_BATCH:-1}"
LATENCY_SAMPLE_RATE="${LATENCY_SAMPLE_RATE:-1}"
BASE_PORT="${BASE_PORT:-27210}"
SCENARIO_PREFIX="${SCENARIO_PREFIX:-}"

RUN_S3="${RUN_S3:-0}"
RUN_S4="${RUN_S4:-0}"

RESULT_ROOT="${SCRIPT_DIR}/../result"
METRICS_CSV="${METRICS_CSV:-${RESULT_ROOT}/metrics.csv}"
LOG_FILE="${LOG_FILE:-${RESULT_ROOT}/scenario.log}"

mkdir -p "$(dirname "${METRICS_CSV}")"
mkdir -p "$(dirname "${LOG_FILE}")"

if [[ ! -x "${BIN}" ]]; then
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DZLINK_BUILD_TESTS=ON >/dev/null
  cmake --build "${BUILD_DIR}" --target test_scenario_stream_asio -j"$(nproc)"
fi

export LD_LIBRARY_PATH="${BUILD_DIR}/lib:${LD_LIBRARY_PATH:-}"

overall_rc=0

run_case() {
  local scenario_id="$1"
  local scenario="$2"
  local transport="$3"
  local port="$4"
  shift 4
  local resolved_scenario_id="${SCENARIO_PREFIX}${scenario_id}"

  {
    echo
    echo "===== asio ${resolved_scenario_id} ====="
    echo "scenario=${scenario} transport=${transport} ccu=${CCU} size=${SIZE} inflight=${INFLIGHT} port=${port}"
  } | tee -a "${LOG_FILE}"

  if ! "${BIN}" \
      --scenario "${scenario}" \
      --scenario-id "${resolved_scenario_id}" \
      --transport "${transport}" \
      --port "${port}" \
      --ccu "${CCU}" \
      --size "${SIZE}" \
      --inflight "${INFLIGHT}" \
      --warmup "${WARMUP}" \
      --measure "${MEASURE}" \
      --drain-timeout "${DRAIN_TIMEOUT}" \
      --connect-concurrency "${CONNECT_CONCURRENCY}" \
      --connect-timeout "${CONNECT_TIMEOUT}" \
      --backlog "${BACKLOG}" \
      --sndbuf "${SNDBUF}" \
      --rcvbuf "${RCVBUF}" \
      --no-delay "${NO_DELAY}" \
      --io-threads "${IO_THREADS}" \
      --send-batch "${SEND_BATCH}" \
      --latency-sample-rate "${LATENCY_SAMPLE_RATE}" \
      --metrics-csv "${METRICS_CSV}" "$@" 2>&1 | tee -a "${LOG_FILE}"; then
    overall_rc=1
  fi
}

run_case "s0" "s0" "${TRANSPORT}" "${BASE_PORT}" --ccu 1 --inflight 1 --warmup 1 --measure 1
run_case "s1" "s1" "${TRANSPORT}" "$((BASE_PORT + 1))"
run_case "s2" "s2" "${TRANSPORT}" "$((BASE_PORT + 2))"

if [[ "${RUN_S3}" == "1" ]]; then
  for v in 8192 32768 65535; do
    run_case "s3-backlog-${v}" "s2" "${TRANSPORT}" "$((BASE_PORT + 20 + v % 10))" --backlog "${v}"
  done

  for v in 65536 262144 1048576; do
    run_case "s3-buf-${v}" "s2" "${TRANSPORT}" "$((BASE_PORT + 40 + v % 10))" --sndbuf "${v}" --rcvbuf "${v}"
  done

  for v in 128 256 512; do
    run_case "s3-connect-${v}" "s2" "${TRANSPORT}" "$((BASE_PORT + 60 + v % 10))" --connect-concurrency "${v}"
  done
fi

if [[ "${RUN_S4}" == "1" ]]; then
  local_port=$((BASE_PORT + 100))
  for tr in tcp tls ws wss; do
    run_case "s4-${tr}-s0" "s0" "${tr}" "${local_port}"
    local_port=$((local_port + 1))
    run_case "s4-${tr}-s1" "s1" "${tr}" "${local_port}"
    local_port=$((local_port + 1))
    run_case "s4-${tr}-s2" "s2" "${tr}" "${local_port}"
    local_port=$((local_port + 1))
  done
fi

exit "${overall_rc}"
