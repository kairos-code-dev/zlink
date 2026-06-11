#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${ZLINK_CPP_BUILD_DIR:-$CPP_ROOT/build}"
BIN_DIR="$BUILD_DIR"

if [[ ! -x "$BIN_DIR/sample_cpp_framework_tictactoe_play" && -x "$BIN_DIR/linux-ninja-debug/sample_cpp_framework_tictactoe_play" ]]; then
  BIN_DIR="$BIN_DIR/linux-ninja-debug"
fi

PLAY_BIN="$BIN_DIR/sample_cpp_framework_tictactoe_play"
API_BIN="$BIN_DIR/sample_cpp_framework_tictactoe_api"
CLIENT_BIN="$BIN_DIR/sample_cpp_framework_tictactoe_client"

for binary in "$PLAY_BIN" "$API_BIN" "$CLIENT_BIN"; do
  if [[ ! -x "$binary" ]]; then
    echo "Missing executable: $binary" >&2
    echo "Build C++ samples first or set ZLINK_CPP_BUILD_DIR." >&2
    exit 1
  fi
done

"$PLAY_BIN"
"$API_BIN"

echo "tictactoe server role smoke completed"
echo "tictactoe client executable present: $CLIENT_BIN"
echo "full client/server self-check is not run: current C++ sample channel requests use the local framework runtime and do not complete across separate sample processes."
