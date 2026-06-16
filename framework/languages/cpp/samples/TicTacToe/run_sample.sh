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
PLAY_CHANNEL_ENDPOINT="tcp://127.0.0.1:48104"
PLAY_STREAM_ENDPOINT="tcp://127.0.0.1:48112"
API_CHANNEL_ENDPOINT="tcp://127.0.0.1:48103"
API_HTTP_ENDPOINT="http://127.0.0.1:48113"

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

endpoint_host() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#http://}"
  echo "${endpoint%:*}"
}

endpoint_port() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#http://}"
  echo "${endpoint##*:}"
}

wait_port() {
  local name="$1"
  local endpoint="$2"
  local host
  local port
  host="$(endpoint_host "$endpoint")"
  port="$(endpoint_port "$endpoint")"
  for _ in $(seq 1 100); do
    if (echo >"/dev/tcp/${host}/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name} at ${endpoint}" >&2
  return 1
}

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
wait_port play-channel "$PLAY_CHANNEL_ENDPOINT"
wait_port play-stream "$PLAY_STREAM_ENDPOINT"
wait_port api-channel "$API_CHANNEL_ENDPOINT"
wait_port api-http "$API_HTTP_ENDPOINT"

"$CLIENT_BIN" >"$LOG_DIR/client.log" 2>&1 || {
  cat "$LOG_DIR/client.log" >&2
  cat "$LOG_DIR/play.log" >&2
  cat "$LOG_DIR/api.log" >&2
  exit 1
}

grep -q "stream-inbound sample=TicTacToe" "$LOG_DIR/client.log"
grep -Eq "stream-inbound sample=TicTacToe .* seq=[0-9]" "$LOG_DIR/client.log"
grep -Eq "stream-inbound sample=TicTacToe .* name=.*Notify" "$LOG_DIR/client.log"
echo "tictactoe full client/server self-check completed"
echo "tictactoe actor lifecycle sample gate completed"
