#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT="${SCRIPT_DIR}/StreamSocketScenario.csproj"
DLL="${SCRIPT_DIR}/bin/Release/net8.0/StreamSocketScenario.dll"

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
IO_THREADS="${IO_THREADS:-1}"
LATENCY_SAMPLE_RATE="${LATENCY_SAMPLE_RATE:-1}"
BASE_PORT="${BASE_PORT:-27310}"
SCENARIO_PREFIX="${SCENARIO_PREFIX:-}"

RESULT_ROOT="${SCRIPT_DIR}/../result"
METRICS_CSV="${METRICS_CSV:-${RESULT_ROOT}/metrics.csv}"
LOG_FILE="${LOG_FILE:-${RESULT_ROOT}/scenario.log}"

mkdir -p "$(dirname "${METRICS_CSV}")"
mkdir -p "$(dirname "${LOG_FILE}")"

if [[ ! -f "${DLL}" ]]; then
  dotnet build "${PROJECT}" -c Release >/dev/null
fi

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
    echo "===== dotnet ${resolved_scenario_id} ====="
    echo "scenario=${scenario} transport=${transport} ccu=${CCU} size=${SIZE} inflight=${INFLIGHT} port=${port}"
  } | tee -a "${LOG_FILE}"

  if ! dotnet "${DLL}" \
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
      --io-threads "${IO_THREADS}" \
      --latency-sample-rate "${LATENCY_SAMPLE_RATE}" \
      --metrics-csv "${METRICS_CSV}" "$@" 2>&1 | tee -a "${LOG_FILE}"; then
    overall_rc=1
  fi
}

run_case "s0" "s0" "${TRANSPORT}" "${BASE_PORT}" --ccu 1 --inflight 1 --warmup 1 --measure 1
run_case "s1" "s1" "${TRANSPORT}" "$((BASE_PORT + 1))"
run_case "s2" "s2" "${TRANSPORT}" "$((BASE_PORT + 2))"

exit "${overall_rc}"
