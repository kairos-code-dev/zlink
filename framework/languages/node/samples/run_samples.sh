#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NODE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

cd "${NODE_ROOT}"
npm run build >/dev/null

"${SCRIPT_DIR}/TicTacToe.Ts/run_sample.sh"
"${SCRIPT_DIR}/Bingo.Ts/run_sample.sh"
