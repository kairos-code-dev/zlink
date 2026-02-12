#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../../../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/core/build"
BIN="${BUILD_DIR}/bin/test_scenario_router_router"
RESULT_ROOT="${SCRIPT_DIR}/result"
LOG_DIR="${LOG_DIR:-${RESULT_ROOT}/$(date +%Y%m%d_%H%M%S)}"
SUMMARY_FILE="${LOG_DIR}/SUMMARY.txt"

mkdir -p "${LOG_DIR}"
printf "Router-Router Scenario Summary\n" >"${SUMMARY_FILE}"
printf "Log directory: %s\n\n" "${LOG_DIR}" >>"${SUMMARY_FILE}"

if [[ ! -x "${BIN}" ]]; then
  cmake -S "${ROOT_DIR}/core" -B "${BUILD_DIR}" -DZLINK_BUILD_TESTS=ON >/dev/null
  cmake --build "${BUILD_DIR}" --target test_scenario_router_router -j"$(nproc)"
fi

export LD_LIBRARY_PATH="${BUILD_DIR}/lib:${LD_LIBRARY_PATH:-}"

overall_rc=0

run_case() {
  local name="$1"
  shift
  echo "===== ${name} ====="
  "${BIN}" "$@" | tee "${LOG_DIR}/${name}.log"
  local rc="${PIPESTATUS[0]}"
  if [[ "${rc}" -eq 0 ]]; then
    printf "%s: PASS\n" "${name}" >>"${SUMMARY_FILE}"
  else
    printf "%s: FAIL (rc=%s)\n" "${name}" "${rc}" >>"${SUMMARY_FILE}"
  fi
  if [[ "${rc}" -ne 0 ]]; then
    overall_rc=1
  fi
}

run_case "LZ-01" --scenario lz-01
run_case "LZ-02" --scenario lz-02 --self-connect 0 --size 64 --ccu 50 --inflight 10 --duration 10 --play-port 16100 --api-port 16201
run_case "LZ-03" --scenario lz-03 --self-connect 1 --size 64 --ccu 50 --inflight 10 --duration 10 --play-port 16110 --api-port 16211
run_case "LZ-04" --scenario lz-04 --self-connect 1 --ccu 200 --inflight 200 --senders 4 --duration 10 --play-port 16120 --api-port 16221
run_case "LZ-05" --scenario lz-05 --self-connect 1 --size 64 --ccu 50 --inflight 20 --senders 2 --duration 15 --reconnect-ms 1000 --reconnect-down-ms 200 --play-port 16130 --api-port 16231

echo "===== SUMMARY ====="
echo "Logs: ${LOG_DIR}"
if [[ "${overall_rc}" -eq 0 ]]; then
  echo "All scenarios passed heuristic checks."
  printf "\nOverall: PASS\n" >>"${SUMMARY_FILE}"
else
  echo "One or more scenarios failed heuristic checks."
  printf "\nOverall: FAIL\n" >>"${SUMMARY_FILE}"
fi

exit "${overall_rc}"
