#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../redis-common.sh"
CPP_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
FLOW_LOG_DIR="$SCRIPT_DIR/logs"
mkdir -p "$FLOW_LOG_DIR"
rm -f "$FLOW_LOG_DIR"/*.log
BUILD_DIR="$CPP_ROOT/build"
BIN_DIR="$BUILD_DIR"

cmake -S "$CPP_ROOT" -B "$BUILD_DIR" -DZLINK_FRAMEWORK_CPP_BUILD_SAMPLES=ON >/dev/null
cmake --build "$BUILD_DIR" --target \
  sample_cpp_framework_tictactoe_play \
  sample_cpp_framework_tictactoe_api \
  sample_cpp_framework_tictactoe_client \
  test_cpp_framework_sample_parity \
  zlink_cpp_framework_mesh_node_vertical_test \
  test_cpp_framework_actor_gateway >/dev/null

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
  -R 'test_cpp_framework_sample_parity|zlink_cpp_framework_mesh_node_vertical_test|test_cpp_framework_actor_gateway|sample_smoke_sample_cpp_framework_tictactoe_(play|api)' \
  --output-on-failure

read -r -a PORTS <<<"$(python3 - <<'PY'
import random
import socket

sockets = []
try:
    chosen = set()
    while len(sockets) < 17:
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

if [[ ${#PORTS[@]} -lt 17 ]]; then
  echo "Failed to allocate 17 local TCP ports for the TicTacToe sample." >&2
  echo "This environment may block local socket creation." >&2
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
API_A_ROUTE_ENDPOINT="tcp://127.0.0.1:${PORTS[15]}"
API_B_ROUTE_ENDPOINT="tcp://127.0.0.1:${PORTS[16]}"

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

RUN_DIR="$(mktemp -d)"
LOG_DIR="$RUN_DIR/logs"
mkdir -p "$LOG_DIR"
PIDS=()
REDIS_CONTAINER=""
cleanup_done=false
TICTACTOE_CPP_REDIS_KEY_PREFIX="zlink:tictactoe-cpp:${RANDOM}:$$:room:"
REDIS_KEY_PREFIX="$TICTACTOE_CPP_REDIS_KEY_PREFIX"

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
  if [[ "$code" -ne 0 || "$cleanup_failed" -ne 0 ]]; then
    rm -f "$FLOW_LOG_DIR"/last-failure-*.log
    for log in "$LOG_DIR"/*.log; do
      [[ -f "$log" ]] || continue
      cp "$log" "$FLOW_LOG_DIR/last-failure-$(basename "$log")"
    done
  fi
  rm -rf "$RUN_DIR"
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
  python3 - "$CONFIG_DIR/$1.json" "$2" "$3" "$FLOW_LOG_DIR" "$API_A_ENDPOINT" \
    "$API_B_ENDPOINT" "$API_A_HTTP_ENDPOINT" "$API_B_HTTP_ENDPOINT" "$PLAY_A_ENDPOINT" \
    "$PLAY_B_ENDPOINT" "$PLAY_A_ROUTE_ENDPOINT" "$PLAY_B_ROUTE_ENDPOINT" \
    "$API_A_ROUTE_ENDPOINT" "$API_B_ROUTE_ENDPOINT" \
    "$PLAY_A_SPOT_ENDPOINT" "$PLAY_B_SPOT_ENDPOINT" "$PLAY_A_SPOT_ROUTER_ENDPOINT" \
    "$PLAY_B_SPOT_ROUTER_ENDPOINT" "$PLAY_A_STREAM_ENDPOINT" "$PLAY_B_STREAM_ENDPOINT" \
    "$TICTACTOE_CPP_REDIS_ENDPOINT" "$REDIS_KEY_PREFIX" <<'CONFIG_PY'
import json
import os
import stat
import sys

(path, api_node, play_node, flow_log_dir, api_a_endpoint, api_b_endpoint,
 api_a_http, api_b_http, play_a_endpoint, play_b_endpoint, play_a_route,
 play_b_route, api_a_route, api_b_route, play_a_spot, play_b_spot, play_a_spot_router,
 play_b_spot_router, play_a_stream, play_b_stream, redis_endpoint,
 redis_key_prefix) = sys.argv[1:]

document = {
    "sample": {
        "host": {"keepRunning": True},
        "topology": {
            "logDir": flow_log_dir,
            "apiNode": api_node,
            "playNode": play_node,
            "apiEndpoint": api_a_endpoint,
            "apiAEndpoint": api_a_endpoint,
            "apiBEndpoint": api_b_endpoint,
            "apiHttpEndpoint": api_a_http,
            "apiAHttpEndpoint": api_a_http,
            "apiBHttpEndpoint": api_b_http,
            "playEndpoint": play_a_endpoint,
            "playAEndpoint": play_a_endpoint,
            "playBEndpoint": play_b_endpoint,
            "playARouteEndpoint": play_a_route,
            "playBRouteEndpoint": play_b_route,
            "apiARouteEndpoint": api_a_route,
            "apiBRouteEndpoint": api_b_route,
            "playASpotEndpoint": play_a_spot,
            "playBSpotEndpoint": play_b_spot,
            "playASpotRouterEndpoint": play_a_spot_router,
            "playBSpotRouterEndpoint": play_b_spot_router,
            "playAStreamEndpoint": play_a_stream,
            "playBStreamEndpoint": play_b_stream,
            "redisEndpoint": redis_endpoint,
            "redisKeyPrefix": redis_key_prefix,
        },
    }
}

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

wait_port play-a-object-route "$PLAY_A_ROUTE_ENDPOINT"
wait_port play-a-stream "$PLAY_A_STREAM_ENDPOINT"
wait_port play-b-object-route "$PLAY_B_ROUTE_ENDPOINT"
wait_port play-b-stream "$PLAY_B_STREAM_ENDPOINT"
wait_port api-a-channel "$API_A_ENDPOINT"
wait_port api-a-http "$API_A_HTTP_ENDPOINT"
wait_port api-a-object-route "$API_A_ROUTE_ENDPOINT"
wait_port api-b-channel "$API_B_ENDPOINT"
wait_port api-b-http "$API_B_HTTP_ENDPOINT"
wait_port api-b-object-route "$API_B_ROUTE_ENDPOINT"

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
wait_grep "observer-win-milestone=verified actor=player-x wins=100" "$LOG_DIR/client.log"
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
