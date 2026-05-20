#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

dotnet run --project "${SCRIPT_DIR}/TicTacToe/Server/TicTacToe.Server.csproj"
"${SCRIPT_DIR}/TicTacToe.SessionGateway/run_sample.sh"
"${SCRIPT_DIR}/Bingo/run_sample.sh"
