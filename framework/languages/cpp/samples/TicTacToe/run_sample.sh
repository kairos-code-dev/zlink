#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../redis-common.sh"
CPP_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
FLOW_LOG_DIR="$SCRIPT_DIR/logs"
mkdir -p "$FLOW_LOG_DIR"
rm -f "$FLOW_LOG_DIR"/*.log
BUILD_DIR="${ZLINK_CPP_BUILD_DIR:-$CPP_ROOT/build}"
BIN_DIR="$BUILD_DIR"

cmake -S "$CPP_ROOT" -B "$BUILD_DIR" -DZLINK_FRAMEWORK_CPP_BUILD_SAMPLES=ON >/dev/null
cmake --build "$BUILD_DIR" --target \
  sample_cpp_framework_tictactoe_play \
  sample_cpp_framework_tictactoe_api \
  sample_cpp_framework_tictactoe_client \
  test_cpp_framework_sample_parity \
  test_cpp_framework_spot_runtime \
  test_cpp_framework_ActorGateway_actor_session_relay >/dev/null

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
    echo "CMake build did not produce the expected TicTacToe sample executable." >&2
    exit 1
  fi
done

"$CTEST_BIN" --test-dir "$BUILD_DIR" \
  -R 'test_cpp_framework_sample_parity|test_cpp_framework_spot_runtime|test_cpp_framework_ActorGateway_actor_session_relay|sample_smoke_sample_cpp_framework_tictactoe_(play|api)' \
  --output-on-failure

if [[ -n "${TICTACTOE_CPP_BASE_PORT:-}" ]]; then
  PORTS=()
  for offset in $(seq 1 15); do
    PORTS+=("$((TICTACTOE_CPP_BASE_PORT + offset))")
  done
else
  read -r -a PORTS <<<"$(python3 - <<'PY'
import random
import socket

sockets = []
try:
    chosen = set()
    while len(sockets) < 15:
        port = random.randint(48000, 60999)
        if port in chosen:
            continue
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            sock.bind(("127.0.0.1", port))
        except OSError:
            sock.close()
            continue
        chosen.add(port)
        sockets.append(sock)
    print(" ".join(str(sock.getsockname()[1]) for sock in sockets))
finally:
    for sock in sockets:
        sock.close()
PY
  )"
fi

if [[ ${#PORTS[@]} -lt 15 ]]; then
  echo "Failed to allocate 15 local TCP ports for the TicTacToe sample." >&2
  echo "This environment may block local socket creation; set TICTACTOE_CPP_BASE_PORT to use fixed ports." >&2
  exit 1
fi

API_A_ENDPOINT="tcp://127.0.0.1:${PORTS[0]}"
API_B_ENDPOINT="tcp://127.0.0.1:${PORTS[1]}"
API_A_HTTP_ENDPOINT="http://127.0.0.1:${PORTS[2]}"
API_B_HTTP_ENDPOINT="http://127.0.0.1:${PORTS[3]}"
PLAY_A_ENDPOINT="tcp://127.0.0.1:${PORTS[4]}"
PLAY_B_ENDPOINT="tcp://127.0.0.1:${PORTS[5]}"
PLAY_A_STREAM_ENDPOINT="tcp://127.0.0.1:${PORTS[6]}"
PLAY_B_STREAM_ENDPOINT="tcp://127.0.0.1:${PORTS[7]}"
PLAY_A_SPOT_ENDPOINT="tcp://127.0.0.1:${PORTS[8]}"
PLAY_B_SPOT_ENDPOINT="tcp://127.0.0.1:${PORTS[9]}"
PLAY_A_SPOT_ROUTER_ENDPOINT="tcp://127.0.0.1:${PORTS[10]}"
PLAY_B_SPOT_ROUTER_ENDPOINT="tcp://127.0.0.1:${PORTS[11]}"
PLAY_A_ROUTE_ENDPOINT="tcp://127.0.0.1:${PORTS[12]}"
PLAY_B_ROUTE_ENDPOINT="tcp://127.0.0.1:${PORTS[13]}"

endpoint_host() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#http://}"
  endpoint="${endpoint#redis://}"
  echo "${endpoint%:*}"
}

endpoint_port() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#http://}"
  endpoint="${endpoint#redis://}"
  echo "${endpoint##*:}"
}

wait_port() {
  local name="$1"
  local endpoint="$2"
  local host
  local port
  host="$(endpoint_host "$endpoint")"
  port="$(endpoint_port "$endpoint")"
  for _ in $(seq 1 120); do
    if (echo >"/dev/tcp/${host}/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name} at ${endpoint}" >&2
  return 1
}

wait_grep() {
  local pattern="$1"
  local file="$2"
  for _ in $(seq 1 50); do
    if grep -q "$pattern" "$file"; then
      return 0
    fi
    sleep 0.1
  done
  grep -q "$pattern" "$file"
}

RUN_DIR="${TICTACTOE_RUN_DIR:-$(mktemp -d)}"
LOG_DIR="$RUN_DIR/logs"
mkdir -p "$LOG_DIR"
PIDS=()
REDIS_CONTAINER=""
cleanup_done=false
REDIS_KEY_PREFIX="${TICTACTOE_CPP_REDIS_KEY_PREFIX:-zlink:tictactoe-cpp:${RANDOM}:$$:room:}"

cleanup() {
  local code=$?
  local cleanup_failed=0
  local status
  if [[ "$cleanup_done" == true ]]; then
    return
  fi
  cleanup_done=true
  for ((i=${#PIDS[@]}-1; i>=0; i--)); do
    kill "${PIDS[$i]}" 2>/dev/null || true
  done
  for pid in "${PIDS[@]}"; do
    set +e
    wait "$pid" >/dev/null 2>&1
    status=$?
    set -e
    if [[ "$status" != "0" && "$status" != "127" && "$status" != "130" && "$status" != "143" ]]; then
      echo "cleanup process $pid exited unexpectedly with status $status" >&2
      cleanup_failed=1
    fi
  done
  if [[ -n "$REDIS_CONTAINER" ]]; then
    docker rm -fv "$REDIS_CONTAINER" >/dev/null 2>&1 || true
  fi
  if [[ "${TICTACTOE_CPP_KEEP_RUN_DIR:-}" == "1" ]]; then
    echo "runDir=$RUN_DIR"
  else
    [[ -z "${TICTACTOE_RUN_DIR:-}" ]] && rm -rf "$RUN_DIR"
  fi
  if [[ "$cleanup_failed" -ne 0 && "$code" -eq 0 ]]; then
    code=1
  fi
  return "$code"
}
trap 'cleanup; status=$?; exit "$status"' EXIT

if ! command -v docker >/dev/null 2>&1; then
  echo "Docker is required to run the TicTacToe sample." >&2
  exit 1
fi
zlink_redis_start_scoped_assign REDIS_CONTAINER redis_port \
  "zlink-redis-cpp-sample-tictactoe" "redis:7-alpine"
TICTACTOE_CPP_REDIS_ENDPOINT="127.0.0.1:${redis_port}"
wait_port redis "$TICTACTOE_CPP_REDIS_ENDPOINT"

CONFIG_DIR="$LOG_DIR/config"
mkdir -p "$CONFIG_DIR"

# 각 role은 자기 설정 파일 하나만 받는다(공통 정책 sample-e2e-configuration-policy.ko.md §2.1).
write_role_config() {
  ROLE="$1" API_NODE="$2" PLAY_NODE="$3" CONFIG_PATH="$CONFIG_DIR/$1.json" \
  FLOW_LOG_DIR="$FLOW_LOG_DIR" API_A_ENDPOINT="$API_A_ENDPOINT" API_B_ENDPOINT="$API_B_ENDPOINT" \
  API_A_HTTP_ENDPOINT="$API_A_HTTP_ENDPOINT" API_B_HTTP_ENDPOINT="$API_B_HTTP_ENDPOINT" \
  PLAY_A_ENDPOINT="$PLAY_A_ENDPOINT" PLAY_B_ENDPOINT="$PLAY_B_ENDPOINT" \
  PLAY_A_ROUTE_ENDPOINT="$PLAY_A_ROUTE_ENDPOINT" PLAY_B_ROUTE_ENDPOINT="$PLAY_B_ROUTE_ENDPOINT" \
  PLAY_A_SPOT_ENDPOINT="$PLAY_A_SPOT_ENDPOINT" PLAY_B_SPOT_ENDPOINT="$PLAY_B_SPOT_ENDPOINT" \
  PLAY_A_SPOT_ROUTER_ENDPOINT="$PLAY_A_SPOT_ROUTER_ENDPOINT" \
  PLAY_B_SPOT_ROUTER_ENDPOINT="$PLAY_B_SPOT_ROUTER_ENDPOINT" \
  PLAY_A_STREAM_ENDPOINT="$PLAY_A_STREAM_ENDPOINT" \
  PLAY_B_STREAM_ENDPOINT="$PLAY_B_STREAM_ENDPOINT" \
  REDIS_ENDPOINT="$TICTACTOE_CPP_REDIS_ENDPOINT" REDIS_KEY_PREFIX="$REDIS_KEY_PREFIX" \
  python3 - <<'CONFIG_PY'
import json
import os
import stat

document = {
    "sample": {
        "host": {"keepRunning": True},
        "topology": {
            "logDir": os.environ["FLOW_LOG_DIR"],
            "apiNode": os.environ["API_NODE"],
            "playNode": os.environ["PLAY_NODE"],
            "apiEndpoint": os.environ["API_A_ENDPOINT"],
            "apiAEndpoint": os.environ["API_A_ENDPOINT"],
            "apiBEndpoint": os.environ["API_B_ENDPOINT"],
            "apiHttpEndpoint": os.environ["API_A_HTTP_ENDPOINT"],
            "apiAHttpEndpoint": os.environ["API_A_HTTP_ENDPOINT"],
            "apiBHttpEndpoint": os.environ["API_B_HTTP_ENDPOINT"],
            "playEndpoint": os.environ["PLAY_A_ENDPOINT"],
            "playAEndpoint": os.environ["PLAY_A_ENDPOINT"],
            "playBEndpoint": os.environ["PLAY_B_ENDPOINT"],
            "playARouteEndpoint": os.environ["PLAY_A_ROUTE_ENDPOINT"],
            "playBRouteEndpoint": os.environ["PLAY_B_ROUTE_ENDPOINT"],
            "playASpotEndpoint": os.environ["PLAY_A_SPOT_ENDPOINT"],
            "playBSpotEndpoint": os.environ["PLAY_B_SPOT_ENDPOINT"],
            "playASpotRouterEndpoint": os.environ["PLAY_A_SPOT_ROUTER_ENDPOINT"],
            "playBSpotRouterEndpoint": os.environ["PLAY_B_SPOT_ROUTER_ENDPOINT"],
            "playAStreamEndpoint": os.environ["PLAY_A_STREAM_ENDPOINT"],
            "playBStreamEndpoint": os.environ["PLAY_B_STREAM_ENDPOINT"],
            "redisEndpoint": os.environ["REDIS_ENDPOINT"],
            "redisKeyPrefix": os.environ["REDIS_KEY_PREFIX"],
        },
    }
}

path = os.environ["CONFIG_PATH"]
with open(path, "w", encoding="utf-8") as file:
    json.dump(document, file, indent=2)
os.chmod(path, stat.S_IRUSR | stat.S_IWUSR)
CONFIG_PY
}

write_role_config play-a a a
write_role_config play-b a b
write_role_config api-a a a
write_role_config api-b b a

start_server() {
  local name="$1"
  local binary="$2"
  shift 2
  stdbuf -oL -eL "$binary" "$@" >"$LOG_DIR/${name}.log" 2>&1 &
  PIDS+=("$!")
}

start_server play-a "$PLAY_BIN" --config="$CONFIG_DIR/play-a.json"
start_server play-b "$PLAY_BIN" --config="$CONFIG_DIR/play-b.json"
start_server api-a "$API_BIN" --config="$CONFIG_DIR/api-a.json"
start_server api-b "$API_BIN" --config="$CONFIG_DIR/api-b.json"

wait_port play-a-channel "$PLAY_A_ENDPOINT"
wait_port play-a-stream "$PLAY_A_STREAM_ENDPOINT"
wait_port play-a-spot-router "$PLAY_A_SPOT_ROUTER_ENDPOINT"
wait_port play-a-spot-pub "$PLAY_A_SPOT_ENDPOINT"
wait_port play-b-channel "$PLAY_B_ENDPOINT"
wait_port play-b-stream "$PLAY_B_STREAM_ENDPOINT"
wait_port play-b-spot-router "$PLAY_B_SPOT_ROUTER_ENDPOINT"
wait_port play-b-spot-pub "$PLAY_B_SPOT_ENDPOINT"
wait_port api-a-channel "$API_A_ENDPOINT"
wait_port api-a-http "$API_A_HTTP_ENDPOINT"
wait_port api-b-channel "$API_B_ENDPOINT"
wait_port api-b-http "$API_B_HTTP_ENDPOINT"

if [[ "${TICTACTOE_CPP_STARTUP_SETTLE_SECONDS:-0}" != "0" ]]; then
  sleep "$TICTACTOE_CPP_STARTUP_SETTLE_SECONDS"
fi

"$CLIENT_BIN" --api-http-endpoint "$API_A_HTTP_ENDPOINT" >"$LOG_DIR/client.log" 2>&1 || {
  cat "$LOG_DIR/client.log" >&2
  cat "$LOG_DIR/play-a.log" >&2
  cat "$LOG_DIR/play-b.log" >&2
  cat "$LOG_DIR/api-a.log" >&2
  cat "$LOG_DIR/api-b.log" >&2
  exit 1
}

wait_grep "observer-connected endpoint=tcp://127.0.0.1:" "$LOG_DIR/client.log"
wait_grep "observer-subscription=verified subscribed=true" "$LOG_DIR/client.log"
wait_grep "observer-win-milestone=verified actor=player-x wins=100 receivingSpotNodeRid=play-node-" "$LOG_DIR/client.log"
wait_grep "tictactoe completed" "$LOG_DIR/client.log"
grep -q "stream-inbound sample=TicTacToe" "$LOG_DIR/client.log"
grep -Eq "stream-inbound sample=TicTacToe .* seq=[0-9]" "$LOG_DIR/client.log"
wait_grep "tictactoe=completed" "$LOG_DIR/client.log"
grep -q "actor: LeaveGameReq completed. actor=player-x" "$LOG_DIR"/play-*.log
grep -q "actor: LeaveGameReq completed. actor=player-o" "$LOG_DIR"/play-*.log
grep -q "entry spot: actor destroy completed. actor=player-x" "$LOG_DIR"/play-*.log
grep -q "entry spot: actor destroy completed. actor=player-o" "$LOG_DIR"/play-*.log
grep -Rq "packet=LeaveGameReq" "$FLOW_LOG_DIR"
grep -Rq "message flow" "$FLOW_LOG_DIR"

cleanup
trap - EXIT

echo "PASS TicTacToe.Cpp"
echo "tictactoe full client/server self-check completed"
