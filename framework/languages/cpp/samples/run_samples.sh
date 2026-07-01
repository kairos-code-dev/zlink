#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

"$SCRIPT_DIR/TicTacToe/run_sample.sh"
"$SCRIPT_DIR/Bingo/run_sample.sh"
"$SCRIPT_DIR/DeliveryDispatch/run_sample.sh"
