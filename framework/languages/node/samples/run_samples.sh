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

"${SCRIPT_DIR}/TicTacToe.Ts/run_sample.sh"
"${SCRIPT_DIR}/Bingo.Ts/run_sample.sh"
