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
CTEST_BIN="${CTEST_BIN:-ctest}"
REGISTRY_PUB_ENDPOINT="tcp://127.0.0.1:47101"
REGISTRY_ROUTER_ENDPOINT="tcp://127.0.0.1:47102"
API_CHANNEL_ENDPOINT="tcp://127.0.0.1:47103"
PLAY_CHANNEL_ENDPOINT="tcp://127.0.0.1:47104"
SESSION_STREAM_ENDPOINT="tcp://127.0.0.1:47114"

for binary in "$REGISTRY_BIN" "$API_BIN" "$PLAY_BIN" "$SESSION_BIN" "$CLIENT_BIN"; do
  if [[ ! -x "$binary" ]]; then
    echo "Missing executable: $binary" >&2
    echo "Build C++ samples first or set ZLINK_CPP_BUILD_DIR." >&2
    exit 1
  fi
done

"$CTEST_BIN" --test-dir "$BUILD_DIR" \
  -R 'test_cpp_framework_sample_parity|test_cpp_framework_spot_runtime|test_cpp_framework_ActorGateway_actor_session_relay|sample_smoke_sample_cpp_framework_bingo_(registry|api|play|session)' \
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
REGISTRY_PID=""
API_PID=""
PLAY_PID=""
SESSION_PID=""
cleanup_done=false
cleanup() {
  if [[ "$cleanup_done" == true ]]; then
    return
  fi
  cleanup_done=true
  if [[ -n "$SESSION_PID" ]]; then
    kill "$SESSION_PID" 2>/dev/null || true
    wait "$SESSION_PID" 2>/dev/null || true
    SESSION_PID=""
  fi
  if [[ -n "$PLAY_PID" ]]; then
    kill "$PLAY_PID" 2>/dev/null || true
    wait "$PLAY_PID" 2>/dev/null || true
    PLAY_PID=""
  fi
  if [[ -n "$API_PID" ]]; then
    kill "$API_PID" 2>/dev/null || true
    wait "$API_PID" 2>/dev/null || true
    API_PID=""
  fi
  if [[ -n "$REGISTRY_PID" ]]; then
    kill "$REGISTRY_PID" 2>/dev/null || true
    wait "$REGISTRY_PID" 2>/dev/null || true
    REGISTRY_PID=""
  fi
}
trap cleanup EXIT

"$REGISTRY_BIN" --sample.host.keepRunning true >"$LOG_DIR/registry.log" 2>&1 &
REGISTRY_PID=$!
"$API_BIN" --sample.host.keepRunning true >"$LOG_DIR/api.log" 2>&1 &
API_PID=$!
"$PLAY_BIN" --sample.host.keepRunning true >"$LOG_DIR/play.log" 2>&1 &
PLAY_PID=$!
"$SESSION_BIN" --sample.host.keepRunning true >"$LOG_DIR/session.log" 2>&1 &
SESSION_PID=$!
wait_port registry-pub "$REGISTRY_PUB_ENDPOINT"
wait_port registry-router "$REGISTRY_ROUTER_ENDPOINT"
wait_port api-channel "$API_CHANNEL_ENDPOINT"
wait_port play-channel "$PLAY_CHANNEL_ENDPOINT"
wait_port session-stream "$SESSION_STREAM_ENDPOINT"

"$CLIENT_BIN" >"$LOG_DIR/client.log" 2>&1 || {
  cat "$LOG_DIR/client.log" >&2
  cat "$LOG_DIR/session.log" >&2
  cat "$LOG_DIR/play.log" >&2
  cat "$LOG_DIR/api.log" >&2
  cat "$LOG_DIR/registry.log" >&2
  exit 1
}

grep -q "stream-inbound sample=Bingo" "$LOG_DIR/client.log"
grep -Eq "stream-inbound sample=Bingo .* seq=[0-9]" "$LOG_DIR/client.log"
grep -Eq "stream-inbound sample=Bingo .* name=.*Notify" "$LOG_DIR/client.log"
cleanup
trap - EXIT

echo "bingo full client/server self-check completed"
echo "bingo actor lifecycle sample gate completed"
