#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../redis-common.sh"
CPP_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
FLOW_LOG_DIR="${SCRIPT_DIR}/logs"
mkdir -p "$FLOW_LOG_DIR"
rm -f "$FLOW_LOG_DIR"/*.log
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

cmake --build "$BUILD_DIR" --target \
  sample_cpp_framework_gamequest_game_api \
  sample_cpp_framework_gamequest_quest_mission \
  sample_cpp_framework_gamequest_client >/dev/null

zlink_redis_start_scoped_assign REDIS_CONTAINER_NAME redis_port \
  "zlink-redis-cpp-sample-gamequest" "redis:7-alpine"
GAMEQUEST_REDIS_ENDPOINT="tcp://127.0.0.1:${redis_port}"
GAMEQUEST_REDIS_KEY_PREFIX_BASE="${GAMEQUEST_REDIS_KEY_PREFIX:-gamequest:cpp:}"
GAMEQUEST_REDIS_KEY_PREFIX="${GAMEQUEST_REDIS_KEY_PREFIX_BASE%:}:${RUN_ID}:"
GAMEQUEST_API_A_STREAM_ENDPOINT="tcp://127.0.0.1:${GAMEQUEST_API_A_STREAM_PORT}"
GAMEQUEST_API_B_STREAM_ENDPOINT="tcp://127.0.0.1:${GAMEQUEST_API_B_STREAM_PORT}"
GAMEQUEST_API_A_HTTP_URL="http://127.0.0.1:${GAMEQUEST_API_A_HTTP_PORT}"
GAMEQUEST_API_B_HTTP_URL="http://127.0.0.1:${GAMEQUEST_API_B_HTTP_PORT}"
GAMEQUEST_MISSION_A_ROUTE_ENDPOINT="tcp://127.0.0.1:${GAMEQUEST_MISSION_A_ROUTE_PORT}"
GAMEQUEST_MISSION_B_ROUTE_ENDPOINT="tcp://127.0.0.1:${GAMEQUEST_MISSION_B_ROUTE_PORT}"
GAMEQUEST_MISSION_A_SPOT_ROUTE_ENDPOINT="tcp://127.0.0.1:${GAMEQUEST_MISSION_A_SPOT_ROUTE_PORT}"
GAMEQUEST_MISSION_B_SPOT_ROUTE_ENDPOINT="tcp://127.0.0.1:${GAMEQUEST_MISSION_B_SPOT_ROUTE_PORT}"
GAMEQUEST_MISSION_A_SPOT_ROUTER_ENDPOINT="tcp://127.0.0.1:${GAMEQUEST_MISSION_A_SPOT_ROUTER_PORT}"
GAMEQUEST_MISSION_B_SPOT_ROUTER_ENDPOINT="tcp://127.0.0.1:${GAMEQUEST_MISSION_B_SPOT_ROUTER_PORT}"
GAMEQUEST_MISSION_A_SPOT_ENDPOINT="tcp://127.0.0.1:${GAMEQUEST_MISSION_A_SPOT_PORT}"
GAMEQUEST_MISSION_B_SPOT_ENDPOINT="tcp://127.0.0.1:${GAMEQUEST_MISSION_B_SPOT_PORT}"
GAMEQUEST_API_A_SPOT_ROUTE="tcp://127.0.0.1:${GAMEQUEST_API_A_SPOT_ROUTE_PORT}"
GAMEQUEST_API_B_SPOT_ROUTE="tcp://127.0.0.1:${GAMEQUEST_API_B_SPOT_ROUTE_PORT}"
GAMEQUEST_API_A_SPOT_ROUTER_ENDPOINT="tcp://127.0.0.1:${GAMEQUEST_API_A_SPOT_ROUTER_PORT}"
GAMEQUEST_API_B_SPOT_ROUTER_ENDPOINT="tcp://127.0.0.1:${GAMEQUEST_API_B_SPOT_ROUTER_PORT}"

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
  for log in "$LOG_DIR"/*.log "$FLOW_LOG_DIR"/flow-*.log; do
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

wait_port redis "$GAMEQUEST_REDIS_ENDPOINT"

CONFIG_DIR="$RUN_DIR/config"
mkdir -p "$CONFIG_DIR"

# 각 role은 자기 설정 파일 하나만 받는다(공통 정책 sample-e2e-configuration-policy.ko.md §2.1).
write_role_config() {
  ROLE="$1" API_NAME="$2" MISSION_NAME="$3" CONFIG_PATH="$CONFIG_DIR/$1.json" \
  FLOW_LOG_DIR="$FLOW_LOG_DIR" REDIS_ENDPOINT="$GAMEQUEST_REDIS_ENDPOINT" \
  REDIS_KEY_PREFIX="$GAMEQUEST_REDIS_KEY_PREFIX" \
  API_A_STREAM="$GAMEQUEST_API_A_STREAM_ENDPOINT" API_B_STREAM="$GAMEQUEST_API_B_STREAM_ENDPOINT" \
  API_A_HTTP="$GAMEQUEST_API_A_HTTP_URL" API_B_HTTP="$GAMEQUEST_API_B_HTTP_URL" \
  MISSION_A_ROUTE="$GAMEQUEST_MISSION_A_ROUTE_ENDPOINT" \
  MISSION_B_ROUTE="$GAMEQUEST_MISSION_B_ROUTE_ENDPOINT" \
  MISSION_A_SPOT_ROUTE="$GAMEQUEST_MISSION_A_SPOT_ROUTE_ENDPOINT" \
  MISSION_B_SPOT_ROUTE="$GAMEQUEST_MISSION_B_SPOT_ROUTE_ENDPOINT" \
  MISSION_A_SPOT_ROUTER="$GAMEQUEST_MISSION_A_SPOT_ROUTER_ENDPOINT" \
  MISSION_B_SPOT_ROUTER="$GAMEQUEST_MISSION_B_SPOT_ROUTER_ENDPOINT" \
  MISSION_A_SPOT="$GAMEQUEST_MISSION_A_SPOT_ENDPOINT" \
  MISSION_B_SPOT="$GAMEQUEST_MISSION_B_SPOT_ENDPOINT" \
  API_A_SPOT_ROUTER="$GAMEQUEST_API_A_SPOT_ROUTER_ENDPOINT" \
  API_B_SPOT_ROUTER="$GAMEQUEST_API_B_SPOT_ROUTER_ENDPOINT" \
  API_A_SPOT_ROUTE="$GAMEQUEST_API_A_SPOT_ROUTE" API_B_SPOT_ROUTE="$GAMEQUEST_API_B_SPOT_ROUTE" \
  python3 - <<'CONFIG_PY'
import json
import os
import stat

document = {
    "sample": {
        "role": {"name": os.environ["ROLE"], "logDir": os.environ["FLOW_LOG_DIR"]},
        "topology": {
            "redisEndpoint": os.environ["REDIS_ENDPOINT"],
            "redisKeyPrefix": os.environ["REDIS_KEY_PREFIX"],
            "apiAStreamEndpoint": os.environ["API_A_STREAM"],
            "apiBStreamEndpoint": os.environ["API_B_STREAM"],
            "apiAHttpUrl": os.environ["API_A_HTTP"],
            "apiBHttpUrl": os.environ["API_B_HTTP"],
            "missionARouteEndpoint": os.environ["MISSION_A_ROUTE"],
            "missionBRouteEndpoint": os.environ["MISSION_B_ROUTE"],
            "missionASpotRouteEndpoint": os.environ["MISSION_A_SPOT_ROUTE"],
            "missionBSpotRouteEndpoint": os.environ["MISSION_B_SPOT_ROUTE"],
            "missionASpotRouterEndpoint": os.environ["MISSION_A_SPOT_ROUTER"],
            "missionBSpotRouterEndpoint": os.environ["MISSION_B_SPOT_ROUTER"],
            "missionASpotEndpoint": os.environ["MISSION_A_SPOT"],
            "missionBSpotEndpoint": os.environ["MISSION_B_SPOT"],
            "apiASpotRouterEndpoint": os.environ["API_A_SPOT_ROUTER"],
            "apiBSpotRouterEndpoint": os.environ["API_B_SPOT_ROUTER"],
            "apiName": os.environ["API_NAME"],
            "missionName": os.environ["MISSION_NAME"],
            "apiASpotRouteEndpoint": os.environ["API_A_SPOT_ROUTE"],
            "apiBSpotRouteEndpoint": os.environ["API_B_SPOT_ROUTE"],
        },
    }
}

path = os.environ["CONFIG_PATH"]
with open(path, "w", encoding="utf-8") as file:
    json.dump(document, file, indent=2)
os.chmod(path, stat.S_IRUSR | stat.S_IWUSR)
CONFIG_PY
}

write_role_config mission-a api-a mission-a
write_role_config mission-b api-a mission-b
write_role_config api-a api-a mission-a
write_role_config api-b api-b mission-a

start_role mission-a "$BIN_DIR/sample_cpp_framework_gamequest_quest_mission" --config="$CONFIG_DIR/mission-a.json"
start_role mission-b "$BIN_DIR/sample_cpp_framework_gamequest_quest_mission" --config="$CONFIG_DIR/mission-b.json"
start_role api-a "$BIN_DIR/sample_cpp_framework_gamequest_game_api" --config="$CONFIG_DIR/api-a.json"
start_role api-b "$BIN_DIR/sample_cpp_framework_gamequest_game_api" --config="$CONFIG_DIR/api-b.json"

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

"$BIN_DIR/sample_cpp_framework_gamequest_client" \
  --api-a-stream-endpoint "$GAMEQUEST_API_A_STREAM_ENDPOINT" \
  --api-b-stream-endpoint "$GAMEQUEST_API_B_STREAM_ENDPOINT" \
  --api-a-http-url "$GAMEQUEST_API_A_HTTP_URL" \
  --api-b-http-url "$GAMEQUEST_API_B_HTTP_URL" >"$LOG_DIR/client.log" 2>&1 || {
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
grep -Rq "message flow" "$FLOW_LOG_DIR"
grep -q "message flow" "$FLOW_LOG_DIR/flow-api-a.log"
grep -q "message flow" "$FLOW_LOG_DIR/flow-api-b.log"
grep -q "message flow" "$FLOW_LOG_DIR/flow-mission-a.log"
grep -q "message flow" "$FLOW_LOG_DIR/flow-mission-b.log"

echo "PASS GameQuest.Cpp"
echo "gamequest sample result=passed"
