#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../redis-common.sh"
CPP_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
export GAMEQUEST_LOG_DIR="${GAMEQUEST_LOG_DIR:-${SCRIPT_DIR}/logs}"
mkdir -p "$GAMEQUEST_LOG_DIR"
rm -f "$GAMEQUEST_LOG_DIR"/*.log
BUILD_DIR="${ZLINK_CPP_BUILD_DIR:-$CPP_ROOT/build}"
BIN_DIR="$BUILD_DIR"
cmake -S "$CPP_ROOT" -B "$BUILD_DIR" -DZLINK_FRAMEWORK_CPP_BUILD_SAMPLES=ON >/dev/null
if [[ ! -x "$BIN_DIR/sample_cpp_framework_gamequest_client" && -x "$BIN_DIR/linux-ninja-debug/sample_cpp_framework_gamequest_client" ]]; then
  BIN_DIR="$BIN_DIR/linux-ninja-debug"
fi

PIDS=()
RUN_DIR="${GAMEQUEST_RUN_DIR:-$(mktemp -d)}"
RUN_ID="$(basename "$RUN_DIR")-$$-${RANDOM}"
LOG_DIR="$RUN_DIR/logs"
REDIS_CONTAINER_NAME=""
mkdir -p "$LOG_DIR"

cleanup() {
  local code=$?
  local cleanup_failed=0
  local status
  for pid in "${PIDS[@]}"; do
    if kill -0 "${pid}" >/dev/null 2>&1; then
      kill "${pid}" >/dev/null 2>&1 || true
      for _ in $(seq 1 40); do
        if ! kill -0 "${pid}" >/dev/null 2>&1; then
          break
        fi
        sleep 0.05
      done
      if kill -0 "${pid}" >/dev/null 2>&1; then
        echo "forced cleanup process ${pid}" >&2
        kill -9 "${pid}" >/dev/null 2>&1 || true
        cleanup_failed=1
      fi
    fi
    set +e
    wait "${pid}" 2>/dev/null
    status=$?
    set -e
    if [[ "$status" != "0" && "$status" != "127" && "$status" != "130" && "$status" != "143" ]]; then
      echo "cleanup process ${pid} exited unexpectedly with status ${status}" >&2
      cleanup_failed=1
    fi
  done
  if [[ -n "$REDIS_CONTAINER_NAME" ]]; then
    docker rm -fv "$REDIS_CONTAINER_NAME" >/dev/null 2>&1 || true
  fi
  if [[ "${GAMEQUEST_KEEP_RUN_DIR:-}" == "1" ]]; then
    echo "runDir=$RUN_DIR"
  else
    [[ -z "${GAMEQUEST_RUN_DIR:-}" ]] && rm -rf "$RUN_DIR"
  fi
  if [[ "$cleanup_failed" -ne 0 && "$code" -eq 0 ]]; then
    code=1
  fi
  return "$code"
}
trap 'cleanup; status=$?; exit "$status"' EXIT

if [[ -n "${GAMEQUEST_CPP_BASE_PORT:-}" ]]; then
  GAMEQUEST_RESERVED_PORT="$((GAMEQUEST_CPP_BASE_PORT + 1))"
  GAMEQUEST_API_A_STREAM_PORT="$((GAMEQUEST_CPP_BASE_PORT + 2))"
  GAMEQUEST_API_B_STREAM_PORT="$((GAMEQUEST_CPP_BASE_PORT + 3))"
  GAMEQUEST_API_A_HTTP_PORT="$((GAMEQUEST_CPP_BASE_PORT + 4))"
  GAMEQUEST_API_B_HTTP_PORT="$((GAMEQUEST_CPP_BASE_PORT + 5))"
  GAMEQUEST_MISSION_A_ROUTE_PORT="$((GAMEQUEST_CPP_BASE_PORT + 6))"
  GAMEQUEST_MISSION_B_ROUTE_PORT="$((GAMEQUEST_CPP_BASE_PORT + 7))"
  GAMEQUEST_MISSION_A_SPOT_ROUTE_PORT="$((GAMEQUEST_CPP_BASE_PORT + 8))"
  GAMEQUEST_MISSION_B_SPOT_ROUTE_PORT="$((GAMEQUEST_CPP_BASE_PORT + 9))"
  GAMEQUEST_MISSION_A_SPOT_ROUTER_PORT="$((GAMEQUEST_CPP_BASE_PORT + 10))"
  GAMEQUEST_MISSION_B_SPOT_ROUTER_PORT="$((GAMEQUEST_CPP_BASE_PORT + 11))"
  GAMEQUEST_MISSION_A_SPOT_PORT="$((GAMEQUEST_CPP_BASE_PORT + 12))"
  GAMEQUEST_MISSION_B_SPOT_PORT="$((GAMEQUEST_CPP_BASE_PORT + 13))"
  GAMEQUEST_API_A_SPOT_ROUTER_PORT="$((GAMEQUEST_CPP_BASE_PORT + 14))"
  GAMEQUEST_API_B_SPOT_ROUTER_PORT="$((GAMEQUEST_CPP_BASE_PORT + 15))"
  GAMEQUEST_API_A_SPOT_ROUTE_PORT="$((GAMEQUEST_CPP_BASE_PORT + 16))"
  GAMEQUEST_API_B_SPOT_ROUTE_PORT="$((GAMEQUEST_CPP_BASE_PORT + 17))"
else
  PORT_ALLOCATION_OUTPUT="$(python3 - <<'PY'
import socket
sockets = []
try:
    for _ in range(17):
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.bind(("127.0.0.1", 0))
        sockets.append(sock)
    print(" ".join(str(sock.getsockname()[1]) for sock in sockets))
except OSError as error:
    print(f"SOCKETLESS {error.errno}:{error.strerror}")
finally:
    for sock in sockets:
        sock.close()
PY
)"
  if [[ "$PORT_ALLOCATION_OUTPUT" == SOCKETLESS* ]]; then
    echo "Failed to allocate local TCP ports for the GameQuest sample: ${PORT_ALLOCATION_OUTPUT#SOCKETLESS }" >&2
    echo "Set GAMEQUEST_CPP_BASE_PORT to use fixed ports when port reservation is blocked." >&2
    exit 1
  fi
  read -r GAMEQUEST_RESERVED_PORT GAMEQUEST_API_A_STREAM_PORT GAMEQUEST_API_B_STREAM_PORT GAMEQUEST_API_A_HTTP_PORT GAMEQUEST_API_B_HTTP_PORT GAMEQUEST_MISSION_A_ROUTE_PORT GAMEQUEST_MISSION_B_ROUTE_PORT GAMEQUEST_MISSION_A_SPOT_ROUTE_PORT GAMEQUEST_MISSION_B_SPOT_ROUTE_PORT GAMEQUEST_MISSION_A_SPOT_ROUTER_PORT GAMEQUEST_MISSION_B_SPOT_ROUTER_PORT GAMEQUEST_MISSION_A_SPOT_PORT GAMEQUEST_MISSION_B_SPOT_PORT GAMEQUEST_API_A_SPOT_ROUTER_PORT GAMEQUEST_API_B_SPOT_ROUTER_PORT GAMEQUEST_API_A_SPOT_ROUTE_PORT GAMEQUEST_API_B_SPOT_ROUTE_PORT <<<"$PORT_ALLOCATION_OUTPUT"
fi
if [[ -z "$GAMEQUEST_RESERVED_PORT" || -z "$GAMEQUEST_API_B_SPOT_ROUTER_PORT" ]]; then
  echo "Failed to allocate local TCP ports for the GameQuest sample." >&2
  echo "This environment may block local socket creation." >&2
  exit 1
fi

zlink_redis_start_scoped_assign REDIS_CONTAINER_NAME redis_port \
  "zlink-redis-cpp-sample-gamequest" "redis:7-alpine"
export GAMEQUEST_REDIS_ENDPOINT="tcp://127.0.0.1:${redis_port}"
GAMEQUEST_REDIS_KEY_PREFIX_BASE="${GAMEQUEST_REDIS_KEY_PREFIX:-gamequest:cpp:}"
export GAMEQUEST_REDIS_KEY_PREFIX="${GAMEQUEST_REDIS_KEY_PREFIX_BASE%:}:${RUN_ID}:"
export GAMEQUEST_API_A_STREAM_ENDPOINT="tcp://127.0.0.1:${GAMEQUEST_API_A_STREAM_PORT}"
export GAMEQUEST_API_B_STREAM_ENDPOINT="tcp://127.0.0.1:${GAMEQUEST_API_B_STREAM_PORT}"
export GAMEQUEST_API_A_HTTP_URL="http://127.0.0.1:${GAMEQUEST_API_A_HTTP_PORT}"
export GAMEQUEST_API_B_HTTP_URL="http://127.0.0.1:${GAMEQUEST_API_B_HTTP_PORT}"
export GAMEQUEST_MISSION_A_ROUTE_ENDPOINT="tcp://127.0.0.1:${GAMEQUEST_MISSION_A_ROUTE_PORT}"
export GAMEQUEST_MISSION_B_ROUTE_ENDPOINT="tcp://127.0.0.1:${GAMEQUEST_MISSION_B_ROUTE_PORT}"
export GAMEQUEST_MISSION_A_SPOT_ROUTE_ENDPOINT="tcp://127.0.0.1:${GAMEQUEST_MISSION_A_SPOT_ROUTE_PORT}"
export GAMEQUEST_MISSION_B_SPOT_ROUTE_ENDPOINT="tcp://127.0.0.1:${GAMEQUEST_MISSION_B_SPOT_ROUTE_PORT}"
export GAMEQUEST_MISSION_A_SPOT_ROUTER_ENDPOINT="tcp://127.0.0.1:${GAMEQUEST_MISSION_A_SPOT_ROUTER_PORT}"
export GAMEQUEST_MISSION_B_SPOT_ROUTER_ENDPOINT="tcp://127.0.0.1:${GAMEQUEST_MISSION_B_SPOT_ROUTER_PORT}"
export GAMEQUEST_MISSION_A_SPOT_ENDPOINT="tcp://127.0.0.1:${GAMEQUEST_MISSION_A_SPOT_PORT}"
export GAMEQUEST_MISSION_B_SPOT_ENDPOINT="tcp://127.0.0.1:${GAMEQUEST_MISSION_B_SPOT_PORT}"
export GAMEQUEST_API_A_SPOT_ROUTE="tcp://127.0.0.1:${GAMEQUEST_API_A_SPOT_ROUTE_PORT}"
export GAMEQUEST_API_B_SPOT_ROUTE="tcp://127.0.0.1:${GAMEQUEST_API_B_SPOT_ROUTE_PORT}"
export GAMEQUEST_API_A_SPOT_ROUTER_ENDPOINT="tcp://127.0.0.1:${GAMEQUEST_API_A_SPOT_ROUTER_PORT}"
export GAMEQUEST_API_B_SPOT_ROUTER_ENDPOINT="tcp://127.0.0.1:${GAMEQUEST_API_B_SPOT_ROUTER_PORT}"
export ZLINK_CPP_AUTO_CONNECT_TRACE="${ZLINK_CPP_AUTO_CONNECT_TRACE-1}"

port_of() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#http://}"
  echo "${endpoint##*:}"
}

wait_port() {
  local label="$1"
  local endpoint="$2"
  local port
  port="$(port_of "$endpoint")"
  for _ in $(seq 1 150); do
    if (echo >"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "timed out waiting for ${label} at ${endpoint}" >&2
  dump_logs
  return 1
}

dump_logs() {
  for log in "$LOG_DIR"/*.log "$GAMEQUEST_LOG_DIR"/flow-*.log; do
    if [[ -f "$log" ]]; then
      echo "===== ${log}" >&2
      cat "$log" >&2
    fi
  done
}

start_role() {
  local name="$1"
  shift
  stdbuf -oL -eL "$@" >"$LOG_DIR/${name}.log" 2>&1 &
  PIDS+=("$!")
}

cmake --build "$BUILD_DIR" --target \
  sample_cpp_framework_gamequest_game_api \
  sample_cpp_framework_gamequest_quest_mission \
  sample_cpp_framework_gamequest_client >/dev/null

wait_port redis "$GAMEQUEST_REDIS_ENDPOINT"

GAMEQUEST_MISSION_NAME="mission-a" start_role mission-a "$BIN_DIR/sample_cpp_framework_gamequest_quest_mission"
GAMEQUEST_MISSION_NAME="mission-b" start_role mission-b "$BIN_DIR/sample_cpp_framework_gamequest_quest_mission"
GAMEQUEST_API_NAME="api-a" start_role api-a "$BIN_DIR/sample_cpp_framework_gamequest_game_api"
GAMEQUEST_API_NAME="api-b" start_role api-b "$BIN_DIR/sample_cpp_framework_gamequest_game_api"

wait_port mission-a-route "$GAMEQUEST_MISSION_A_ROUTE_ENDPOINT"
wait_port mission-a-spot-route "$GAMEQUEST_MISSION_A_SPOT_ROUTE_ENDPOINT"
wait_port mission-a-spot-router "$GAMEQUEST_MISSION_A_SPOT_ROUTER_ENDPOINT"
wait_port mission-a-spot-pub "$GAMEQUEST_MISSION_A_SPOT_ENDPOINT"
wait_port mission-b-route "$GAMEQUEST_MISSION_B_ROUTE_ENDPOINT"
wait_port mission-b-spot-route "$GAMEQUEST_MISSION_B_SPOT_ROUTE_ENDPOINT"
wait_port mission-b-spot-router "$GAMEQUEST_MISSION_B_SPOT_ROUTER_ENDPOINT"
wait_port mission-b-spot-pub "$GAMEQUEST_MISSION_B_SPOT_ENDPOINT"
wait_port api-a-stream "$GAMEQUEST_API_A_STREAM_ENDPOINT"
wait_port api-a-http "$GAMEQUEST_API_A_HTTP_URL"
wait_port api-a-spot-router "$GAMEQUEST_API_A_SPOT_ROUTER_ENDPOINT"
wait_port api-b-stream "$GAMEQUEST_API_B_STREAM_ENDPOINT"
wait_port api-b-http "$GAMEQUEST_API_B_HTTP_URL"
wait_port api-b-spot-router "$GAMEQUEST_API_B_SPOT_ROUTER_ENDPOINT"

sleep "${GAMEQUEST_CPP_STARTUP_SETTLE_SECONDS:-1}"
echo "topology=ready"

"$BIN_DIR/sample_cpp_framework_gamequest_client" >"$LOG_DIR/client.log" 2>&1 || {
  cat "$LOG_DIR/client.log" >&2
  dump_logs
  exit 1
}

grep -q "gamequest-server-evidence=completed" "$LOG_DIR/client.log"
grep -q "gamequest=completed" "$LOG_DIR/client.log"
grep -q "gamequest api event routed" "$LOG_DIR/api-a.log"
grep -q "gamequest api event routed" "$LOG_DIR/api-b.log"
grep -q "gamequest mission processed" "$LOG_DIR/mission-a.log"
grep -q "gamequest mission processed" "$LOG_DIR/mission-b.log"
grep -Rq "message flow" "$GAMEQUEST_LOG_DIR"
grep -q "message flow" "$GAMEQUEST_LOG_DIR/flow-api-a.log"
grep -q "message flow" "$GAMEQUEST_LOG_DIR/flow-api-b.log"
grep -q "message flow" "$GAMEQUEST_LOG_DIR/flow-mission-a.log"
grep -q "message flow" "$GAMEQUEST_LOG_DIR/flow-mission-b.log"

echo "PASS GameQuest.Cpp"
echo "gamequest sample result=passed"
