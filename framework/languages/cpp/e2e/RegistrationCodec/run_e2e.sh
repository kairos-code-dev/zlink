#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${ZLINK_CPP_E2E_BUILD_DIR:-$CPP_DIR/build}"

read -r API ROUTE HTTP CLIENT_ROUTE INVALID <<<"$(python3 - <<'PY'
import socket

sockets = []
ports = []
for _ in range(5):
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    sockets.append(s)
    ports.append(s.getsockname()[1])
print(f"tcp://127.0.0.1:{ports[0]}", end=" ")
print(f"tcp://127.0.0.1:{ports[1]}", end=" ")
print(f"http://127.0.0.1:{ports[2]}", end=" ")
print(f"tcp://127.0.0.1:{ports[3]}", end=" ")
print(f"tcp://127.0.0.1:{ports[4]}")
for s in sockets:
    s.close()
PY
)"

RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$SCRIPT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"
echo "log_dir=$LOG_DIR"

cmake -S "$CPP_DIR" -B "$BUILD_DIR" >/dev/null
cmake --build "$BUILD_DIR" --target \
  zlink_cpp_e2e_registration_codec_server \
  zlink_cpp_e2e_registration_codec_client >/dev/null

SERVER="$BUILD_DIR/zlink_cpp_e2e_registration_codec_server"
CLIENT="$BUILD_DIR/zlink_cpp_e2e_registration_codec_client"
PIDS=()

cleanup() {
  local code=$?
  for pid in "${PIDS[@]:-}"; do
    if kill -0 "$pid" >/dev/null 2>&1; then
      kill "$pid" >/dev/null 2>&1 || true
    fi
  done
  wait >/dev/null 2>&1 || true
  if [[ $code -ne 0 ]]; then
    echo "E2E failed. Logs: $LOG_DIR" >&2
  fi
  exit "$code"
}
trap cleanup EXIT

port_of() {
  local endpoint="$1"
  echo "${endpoint##*:}"
}

wait_port() {
  local name="$1"
  local endpoint="$2"
  local host="127.0.0.1"
  local port
  port="$(port_of "$endpoint")"
  for _ in $(seq 1 100); do
    if (echo >"/dev/tcp/${host}/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.05
  done
  echo "Timed out waiting for $name at $endpoint" >&2
  return 1
}

run_invalid() {
  local mode="$1"
  if ZLINK_CPP_E2E_INVALID_MODE="$mode" \
    ZLINK_CPP_E2E_API_ENDPOINT="$INVALID" \
    ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$SERVER" >"$LOG_DIR/invalid-$mode.stdout.log" 2>"$LOG_DIR/invalid-$mode.stderr.log"; then
    echo "invalid mode $mode unexpectedly succeeded" >&2
    return 1
  fi
  echo "scenario RC-A6 $mode passed"
}

ZLINK_CPP_E2E_API_ENDPOINT="$API" \
ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE" \
ZLINK_CPP_E2E_HTTP_ENDPOINT="$HTTP" \
ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
  "$SERVER" >"$LOG_DIR/server.stdout.log" 2>"$LOG_DIR/server.stderr.log" &
PIDS+=("$!")
wait_port api "$API"
wait_port route "$ROUTE"
wait_port http "$HTTP"

ZLINK_CPP_E2E_API_ENDPOINT="$API" \
ZLINK_CPP_E2E_ROUTE_ENDPOINT="$ROUTE" \
ZLINK_CPP_E2E_CLIENT_ROUTE_ENDPOINT="$CLIENT_ROUTE" \
ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
  "$CLIENT" >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"
cat "$LOG_DIR/client.stdout.log"

run_invalid duplicate
run_invalid wrong-group
run_invalid unsupported-channel
echo "scenario RC-A6 passed"
