#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

dotnet run --project "${SCRIPT_DIR}/TicTacToe/Server/TicTacToe.Server.csproj"
dotnet run --project "${SCRIPT_DIR}/Bingo(session-gateway)/Server/Bingo.SessionGateway.Server.csproj"
