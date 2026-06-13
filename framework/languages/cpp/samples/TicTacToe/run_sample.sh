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
CTEST_BIN="${CTEST_BIN:-ctest}"

for binary in "$PLAY_BIN" "$API_BIN" "$CLIENT_BIN"; do
  if [[ ! -x "$binary" ]]; then
    echo "Missing executable: $binary" >&2
    echo "Build C++ samples first or set ZLINK_CPP_BUILD_DIR." >&2
    exit 1
  fi
done

"$CTEST_BIN" --test-dir "$BUILD_DIR" \
  -R 'test_cpp_framework_sample_parity|test_cpp_framework_spot_runtime|test_cpp_framework_ActorGateway_actor_session_relay|sample_smoke_sample_cpp_framework_tictactoe_(play|api)' \
  --output-on-failure

LOG_DIR="$(mktemp -d)"
PLAY_PID=""
API_PID=""
cleanup() {
  if [[ -n "$PLAY_PID" ]]; then
    kill "$PLAY_PID" 2>/dev/null || true
    wait "$PLAY_PID" 2>/dev/null || true
  fi
  if [[ -n "$API_PID" ]]; then
    kill "$API_PID" 2>/dev/null || true
    wait "$API_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

"$PLAY_BIN" --sample.host.keepRunning true >"$LOG_DIR/play.log" 2>&1 &
PLAY_PID=$!
"$API_BIN" --sample.host.keepRunning true >"$LOG_DIR/api.log" 2>&1 &
API_PID=$!
sleep 2

"$CLIENT_BIN" >"$LOG_DIR/client.log" 2>&1 || {
  cat "$LOG_DIR/client.log" >&2
  cat "$LOG_DIR/play.log" >&2
  cat "$LOG_DIR/api.log" >&2
  exit 1
}

echo "tictactoe full client/server self-check completed"
echo "tictactoe actor lifecycle sample gate completed"
