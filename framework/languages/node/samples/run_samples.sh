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

run_required_sample() {
  local name="$1"
  local script="$2"
  local status
  echo "sample ${name} start"
  set +e
  "${script}"
  status="$?"
  set -e
  if [[ "${status}" == "0" ]]; then
    echo "sample ${name} completed"
    return 0
  fi
  echo "sample ${name} failed with status ${status}" >&2
  return "${status}"
}

run_required_sample TicTacToe.Ts "${SCRIPT_DIR}/TicTacToe.Ts/run_sample.sh"
run_required_sample Bingo.Ts "${SCRIPT_DIR}/Bingo.Ts/run_sample.sh"
run_required_sample DeliveryDispatch.Ts "${SCRIPT_DIR}/DeliveryDispatch.Ts/run_sample.sh"
run_required_sample SupportChat.Ts "${SCRIPT_DIR}/SupportChat.Ts/run_sample.sh"
run_required_sample GameQuest.Ts "${SCRIPT_DIR}/GameQuest.Ts/run_sample.sh"
run_required_sample ShoppingMall.Ts "${SCRIPT_DIR}/ShoppingMall.Ts/run_sample.sh"
