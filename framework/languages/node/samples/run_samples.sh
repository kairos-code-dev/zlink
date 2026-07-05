#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NODE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

cd "${NODE_ROOT}"
npm run build >/dev/null
env -u NODE_TEST_CONTEXT node --test \
  --test-name-pattern 'ZLinkEntrySpotActivation destroyActor does not invoke Entry Spot lifecycle callbacks' \
  test/contract/actor-manager.test.js
echo "node actor lifecycle sample gate completed"

run_sample() {
  local script="$1"
  local attempts="${2:-1}"
  for attempt in $(seq 1 "${attempts}"); do
    if "${script}"; then
      return 0
    fi
    if [[ "${attempt}" == "${attempts}" ]]; then
      return 1
    fi
    echo "retry sample ${script} attempt ${attempt}/${attempts}" >&2
  done
}

run_sample "${SCRIPT_DIR}/TicTacToe.Ts/run_sample.sh" 3
run_sample "${SCRIPT_DIR}/Bingo.Ts/run_sample.sh" 3
run_sample "${SCRIPT_DIR}/DeliveryDispatch.Ts/run_sample.sh" 3
run_sample "${SCRIPT_DIR}/SupportChat.Ts/run_sample.sh" 3
run_sample "${SCRIPT_DIR}/GameQuest.Ts/run_sample.sh" 3
