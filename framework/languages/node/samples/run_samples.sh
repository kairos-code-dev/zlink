#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NODE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

cd "${NODE_ROOT}"
npm run build >/dev/null

node samples/StreamingClient/src/self-check.js
node samples/TicTacToe/client/self-check.js
node samples/TicTacToe.SessionGateway/client/self-check.js
node samples/Bingo/client/self-check.js
