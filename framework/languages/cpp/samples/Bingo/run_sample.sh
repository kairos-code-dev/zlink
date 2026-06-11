#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${ZLINK_CPP_BUILD_DIR:-$CPP_ROOT/build}"
BIN_DIR="$BUILD_DIR"

if [[ ! -x "$BIN_DIR/sample_cpp_framework_bingo_registry" && -x "$BIN_DIR/linux-ninja-debug/sample_cpp_framework_bingo_registry" ]]; then
  BIN_DIR="$BIN_DIR/linux-ninja-debug"
fi

REGISTRY_BIN="$BIN_DIR/sample_cpp_framework_bingo_registry"
API_BIN="$BIN_DIR/sample_cpp_framework_bingo_api"
PLAY_BIN="$BIN_DIR/sample_cpp_framework_bingo_play"
SESSION_BIN="$BIN_DIR/sample_cpp_framework_bingo_session"
CLIENT_BIN="$BIN_DIR/sample_cpp_framework_bingo_client"

for binary in "$REGISTRY_BIN" "$API_BIN" "$PLAY_BIN" "$SESSION_BIN" "$CLIENT_BIN"; do
  if [[ ! -x "$binary" ]]; then
    echo "Missing executable: $binary" >&2
    echo "Build C++ samples first or set ZLINK_CPP_BUILD_DIR." >&2
    exit 1
  fi
done

"$REGISTRY_BIN"
"$API_BIN"
"$PLAY_BIN"
"$SESSION_BIN"

echo "bingo server role smoke completed"
echo "bingo client executable present: $CLIENT_BIN"
echo "full client/server self-check is not run: current C++ sample channel requests use the local framework runtime and do not complete across separate sample processes."
