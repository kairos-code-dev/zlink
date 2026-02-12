#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../../../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/core/build"
BIN="${BUILD_DIR}/bin/test_scenario_router_router_zlink_connect"
RESULT_ROOT="${SCRIPT_DIR}/result"
LOG_DIR="${LOG_DIR:-${RESULT_ROOT}/$(date +%Y%m%d_%H%M%S)}"
SUMMARY_FILE="${LOG_DIR}/SUMMARY.txt"

mkdir -p "${LOG_DIR}"

if [[ ! -x "${BIN}" ]]; then
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DZLINK_BUILD_TESTS=ON >/dev/null
  cmake --build "${BUILD_DIR}" --target test_scenario_router_router_zlink_connect -j"$(nproc)"
fi

export LD_LIBRARY_PATH="${BUILD_DIR}/lib:${LD_LIBRARY_PATH:-}"

overall_rc=0

run_case() {
  local name="$1"
  shift
  local log="${LOG_DIR}/${name}.log"
  echo "===== ${name} ====="
  "${BIN}" "$@" | tee "${log}"
  local rc="${PIPESTATUS[0]}"
  if [[ "${rc}" -ne 0 ]]; then
    overall_rc=1
  fi
  echo "${name}: rc=${rc}" >>"${SUMMARY_FILE}"
}

run_case "LZ-02" --self-connect 0 --size 1024 --ccu 10000 --inflight 10 --duration 10 --senders 1 --play-port 21100 --api-port 21201
run_case "LZ-03" --self-connect 1 --size 1024 --ccu 10000 --inflight 10 --duration 10 --senders 1 --play-port 21300 --api-port 21401

{
  echo
  echo "Log directory: ${LOG_DIR}"
  if [[ "${overall_rc}" -eq 0 ]]; then
    echo "Overall: PASS"
  else
    echo "Overall: FAIL"
  fi
} | tee -a "${SUMMARY_FILE}"

exit "${overall_rc}"
