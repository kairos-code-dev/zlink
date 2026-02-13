#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UPSTREAM_DIR="${SCRIPT_DIR}/upstream"
BUILD_DIR="${UPSTREAM_DIR}/build-local"
UUID_STUB_DIR="${BUILD_DIR}/uuid_stub"
CLIENT_PROJECT="${SCRIPT_DIR}/client_runner/StreamScenarioCppServerClient.csproj"
CLIENT_DLL="${SCRIPT_DIR}/client_runner/bin/Release/net8.0/StreamScenarioCppServerClient.dll"
SERVER_BIN="${BUILD_DIR}/cppserver-performance-tcp_echo_server"
UPSTREAM_URL="${CPPSERVER_UPSTREAM_URL:-https://github.com/chronoxor/CppServer.git}"
UPSTREAM_REF="${CPPSERVER_UPSTREAM_REF:-}"

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
IO_THREADS="${IO_THREADS:-32}"
LATENCY_SAMPLE_RATE="${LATENCY_SAMPLE_RATE:-1}"
BASE_PORT="${BASE_PORT:-27410}"
SCENARIO_PREFIX="${SCENARIO_PREFIX:-}"
STACK_LABEL="${STACK_LABEL:-cppserver}"

RESULT_ROOT="${SCRIPT_DIR}/../result"
METRICS_CSV="${METRICS_CSV:-${RESULT_ROOT}/metrics.csv}"
LOG_FILE="${LOG_FILE:-${RESULT_ROOT}/scenario.log}"

if [[ ! -f "${UPSTREAM_DIR}/CMakeLists.txt" ]]; then
  rm -rf "${UPSTREAM_DIR}"
  git clone --depth 1 "${UPSTREAM_URL}" "${UPSTREAM_DIR}" >/dev/null
  if [[ -n "${UPSTREAM_REF}" ]]; then
    git -C "${UPSTREAM_DIR}" fetch --depth 1 origin "${UPSTREAM_REF}" >/dev/null
    git -C "${UPSTREAM_DIR}" checkout "${UPSTREAM_REF}" >/dev/null
  fi
fi

mkdir -p "$(dirname "${METRICS_CSV}")"
mkdir -p "$(dirname "${LOG_FILE}")"
mkdir -p "${BUILD_DIR}" "${UUID_STUB_DIR}/uuid"

if [[ ! -f "/usr/include/uuid/uuid.h" && ! -f "/usr/local/include/uuid/uuid.h" ]]; then
  cat >"${UUID_STUB_DIR}/uuid/uuid.h" <<'UUIDEOF'
#ifndef UUID_UUID_H
#define UUID_UUID_H

#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t uuid_t[16];

static inline void uuid_generate_time(uuid_t out)
{
    static int seeded = 0;
    if (!seeded) {
        seeded = 1;
        srand((unsigned int)time(NULL));
    }
    for (int i = 0; i < 16; ++i)
        out[i] = (uint8_t)(rand() & 0xFF);
    out[6] = (uint8_t)((out[6] & 0x0F) | 0x10);
    out[8] = (uint8_t)((out[8] & 0x3F) | 0x80);
}

static inline void uuid_generate_random(uuid_t out)
{
    uuid_generate_time(out);
}

#ifdef __cplusplus
}
#endif

#endif
UUIDEOF
fi

cmake -S "${UPSTREAM_DIR}" -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="-I${UUID_STUB_DIR}" >/dev/null

cmake --build "${BUILD_DIR}" --target cppserver-performance-tcp_echo_server -j"$(nproc)" >/dev/null

dotnet build "${CLIENT_PROJECT}" -c Release >/dev/null

overall_rc=0
server_pid=""
control_fifo=""

start_server() {
  local port="$1"
  control_fifo="${BUILD_DIR}/.stream_cppserver_${port}.fifo"
  rm -f "${control_fifo}"
  mkfifo "${control_fifo}"
  exec 9<>"${control_fifo}"
  "${SERVER_BIN}" --port "${port}" --threads "${IO_THREADS}" <"${control_fifo}" >>"${LOG_FILE}" 2>&1 &
  server_pid=$!
  sleep 1
}

stop_server() {
  exec 9>&- 2>/dev/null || true
  rm -f "${control_fifo}" >/dev/null 2>&1 || true

  if [[ -n "${server_pid}" ]] && kill -0 "${server_pid}" 2>/dev/null; then
    kill "${server_pid}" >/dev/null 2>&1 || true
    wait "${server_pid}" >/dev/null 2>&1 || true
  fi

  server_pid=""
  control_fifo=""
}

run_case() {
  local scenario_id="$1"
  local scenario="$2"
  local transport="$3"
  local port="$4"
  shift 4

  local resolved_scenario_id="${SCENARIO_PREFIX}${scenario_id}"

  {
    echo
    echo "===== ${STACK_LABEL} ${resolved_scenario_id} ====="
    echo "scenario=${scenario} transport=${transport} ccu=${CCU} size=${SIZE} inflight=${INFLIGHT} port=${port}"
  } | tee -a "${LOG_FILE}"

  start_server "${port}"
  if ! dotnet "${CLIENT_DLL}" \
      --scenario "${scenario}" \
      --scenario-id "${resolved_scenario_id}" \
      --transport "${transport}" \
      --bind-host "127.0.0.1" \
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
      --io-threads "${IO_THREADS}" \
      --latency-sample-rate "${LATENCY_SAMPLE_RATE}" \
      --metrics-csv "${METRICS_CSV}" "$@" 2>&1 | tee -a "${LOG_FILE}"; then
    overall_rc=1
  fi
  stop_server
}

trap 'stop_server' EXIT

run_case "s0" "s0" "${TRANSPORT}" "${BASE_PORT}" --ccu 1 --inflight 1 --warmup 1 --measure 1
run_case "s1" "s1" "${TRANSPORT}" "$((BASE_PORT + 1))"
run_case "s2" "s2" "${TRANSPORT}" "$((BASE_PORT + 2))"

trap - EXIT
exit "${overall_rc}"
