#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NODE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

cd "${NODE_ROOT}"
npm run build >/dev/null

node samples/StreamingClient/Client/self-check.js
node samples/TicTacToe/Client/self-check.js
node samples/TicTacToe.SessionGateway/Client/self-check.js
node samples/Bingo/Client/self-check.js
