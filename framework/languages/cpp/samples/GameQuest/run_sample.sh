#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
export GAMEQUEST_LOG_DIR="${GAMEQUEST_LOG_DIR:-${SCRIPT_DIR}/logs}"
mkdir -p "$GAMEQUEST_LOG_DIR"
rm -f "$GAMEQUEST_LOG_DIR"/*.log
BUILD_DIR="${ZLINK_CPP_BUILD_DIR:-$CPP_ROOT/build}"
BIN_DIR="$BUILD_DIR"
if [[ ! -x "$BIN_DIR/sample_cpp_framework_gamequest_client" && -x "$BIN_DIR/linux-ninja-debug/sample_cpp_framework_gamequest_client" ]]; then
  BIN_DIR="$BIN_DIR/linux-ninja-debug"
fi

PIDS=()
RUN_DIR="$(mktemp -d)"
LOG_DIR="$RUN_DIR/logs"
mkdir -p "$LOG_DIR"
cleanup() {
  for pid in "${PIDS[@]}"; do
    kill "${pid}" >/dev/null 2>&1 || true
    wait "${pid}" 2>/dev/null || true
  done
}
trap cleanup EXIT

read -r GAMEQUEST_REGISTRY_PUB_ENDPOINT GAMEQUEST_REGISTRY_ROUTER_ENDPOINT GAMEQUEST_FANOUT_PUBLISHER_A_ENDPOINT GAMEQUEST_FANOUT_PUBLISHER_B_ENDPOINT GAMEQUEST_GAMEAPI_A_HTTP_PORT GAMEQUEST_GAMEAPI_B_HTTP_PORT GAMEQUEST_MISSION_A_HTTP_PORT GAMEQUEST_MISSION_B_HTTP_PORT GAMEQUEST_GAMEAPI_A_STREAM_ENDPOINT GAMEQUEST_GAMEAPI_B_STREAM_ENDPOINT <<<"$(python3 - <<'PY'
import socket
sockets = []
for _ in range(10):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    sockets.append(sock)
ports = [sock.getsockname()[1] for sock in sockets]
print(
    f"tcp://127.0.0.1:{ports[0]} "
    f"tcp://127.0.0.1:{ports[1]} "
    f"tcp://127.0.0.1:{ports[2]} "
    f"tcp://127.0.0.1:{ports[3]} "
    f"{ports[4]} {ports[5]} {ports[6]} {ports[7]} "
    f"tcp://127.0.0.1:{ports[8]} "
    f"tcp://127.0.0.1:{ports[9]}"
)
for sock in sockets:
    sock.close()
PY
)"
export GAMEQUEST_REGISTRY_PUB_ENDPOINT
export GAMEQUEST_REGISTRY_ROUTER_ENDPOINT
export GAMEQUEST_FANOUT_PUBLISHER_A_ENDPOINT
export GAMEQUEST_FANOUT_PUBLISHER_B_ENDPOINT
export GAMEQUEST_GAMEAPI_A_HTTP_BASE_URL="http://127.0.0.1:${GAMEQUEST_GAMEAPI_A_HTTP_PORT}"
export GAMEQUEST_GAMEAPI_B_HTTP_BASE_URL="http://127.0.0.1:${GAMEQUEST_GAMEAPI_B_HTTP_PORT}"
export GAMEQUEST_MISSION_A_HTTP_URL="http://127.0.0.1:${GAMEQUEST_MISSION_A_HTTP_PORT}"
export GAMEQUEST_MISSION_B_HTTP_URL="http://127.0.0.1:${GAMEQUEST_MISSION_B_HTTP_PORT}"
export GAMEQUEST_GAMEAPI_A_STREAM_ENDPOINT
export GAMEQUEST_GAMEAPI_B_STREAM_ENDPOINT
export GAMEQUEST_STORE_DIR="$RUN_DIR/store"

port_of() {
  echo "${1##*:}"
}

wait_port() {
  local label="$1"
  local port="$2"
  for _ in $(seq 1 150); do
    if (echo >"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "timed out waiting for ${label} on ${port}" >&2
  return 1
}

start_role() {
  local name="$1"
  shift
  "$@" >"$LOG_DIR/${name}.log" 2>&1 &
  PIDS+=("$!")
}

start_role registry "$BIN_DIR/sample_cpp_framework_gamequest_registry"
wait_port registry "$(port_of "$GAMEQUEST_REGISTRY_ROUTER_ENDPOINT")"

start_role mission-a env GAMEQUEST_MISSION_NAME=mission-a "$BIN_DIR/sample_cpp_framework_gamequest_quest_mission"
wait_port mission-a "$GAMEQUEST_MISSION_A_HTTP_PORT"
start_role mission-b env GAMEQUEST_MISSION_NAME=mission-b "$BIN_DIR/sample_cpp_framework_gamequest_quest_mission"
wait_port mission-b "$GAMEQUEST_MISSION_B_HTTP_PORT"

start_role api-a env GAMEQUEST_API_NAME=api-a "$BIN_DIR/sample_cpp_framework_gamequest_game_api"
wait_port api-a "$GAMEQUEST_GAMEAPI_A_HTTP_PORT"
wait_port api-a-stream "$(port_of "$GAMEQUEST_GAMEAPI_A_STREAM_ENDPOINT")"

"$BIN_DIR/sample_cpp_framework_gamequest_client" \
  --api-url "$GAMEQUEST_GAMEAPI_A_HTTP_BASE_URL" \
  --stream-endpoint "$GAMEQUEST_GAMEAPI_A_STREAM_ENDPOINT" >"$LOG_DIR/client.log" 2>&1 || {
  cat "$LOG_DIR/client.log" >&2
  for log in "$LOG_DIR"/*.log; do
    echo "===== ${log}" >&2
    cat "$log" >&2
  done
  exit 1
}

grep -q "gamequest-server-evidence=completed" "$LOG_DIR/client.log"
grep -Rq "message flow" "$GAMEQUEST_LOG_DIR"
