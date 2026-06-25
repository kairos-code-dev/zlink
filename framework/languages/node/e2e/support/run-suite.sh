#!/usr/bin/env bash
set -euo pipefail

SUITE_NAME="$1"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NODE_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SUITE_DIR="$(cd "${SCRIPT_DIR}/../${SUITE_NAME}" && pwd)"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="${SUITE_DIR}/logs/${RUN_ID}"
RUNNER_TIMEOUT_SECONDS="${ZLINK_NODE_E2E_TIMEOUT_SECONDS:-300}"
mkdir -p "${LOG_DIR}"
echo "log_dir=${LOG_DIR}"

print_logs() {
  local status="$1"
  if [[ "${status}" == "0" ]]; then
    return
  fi
  for log in "${LOG_DIR}"/*.log; do
    [[ -f "${log}" ]] || continue
    echo "===== ${log} =====" >&2
    tail -n 200 "${log}" >&2 || true
  done
}

cleanup() {
  local status="$?"
  set +e
  print_logs "${status}"
  exit "${status}"
}
trap cleanup EXIT

cd "${NODE_ROOT}"
node scripts/ensure_node_binding_dist.js >"${LOG_DIR}/ensure-node-binding-dist.stdout.log" 2>"${LOG_DIR}/ensure-node-binding-dist.stderr.log"
npm run build >"${LOG_DIR}/npm-build.stdout.log" 2>"${LOG_DIR}/npm-build.stderr.log"
timeout "${RUNNER_TIMEOUT_SECONDS}" node "${SCRIPT_DIR}/node-e2e-runner.js" "${SUITE_NAME}" \
  > >(tee "${LOG_DIR}/runner.stdout.log") \
  2> >(tee "${LOG_DIR}/runner.stderr.log" >&2)
