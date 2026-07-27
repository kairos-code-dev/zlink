#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../redis-common.sh"
CPP_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
# Message-flow logs land in the sample's own logs/ folder (git-ignored).
FLOW_LOG_DIR="$SCRIPT_DIR/logs"
mkdir -p "$FLOW_LOG_DIR"
rm -f "$FLOW_LOG_DIR"/*.log
BUILD_DIR="$CPP_ROOT/build"
BIN_DIR="$BUILD_DIR"

cmake -S "$CPP_ROOT" -B "$BUILD_DIR" -DZLINK_FRAMEWORK_CPP_BUILD_SAMPLES=ON >/dev/null
cmake --build "$BUILD_DIR" --target \
  sample_cpp_framework_bingo_api \
  sample_cpp_framework_bingo_play \
  sample_cpp_framework_bingo_session \
  sample_cpp_framework_bingo_client \
  test_cpp_framework_sample_parity \
  zlink_cpp_framework_mesh_node_vertical_test \
  test_cpp_framework_actor_gateway >/dev/null

if [[ ! -x "$BIN_DIR/sample_cpp_framework_bingo_api" && -x "$BIN_DIR/linux-ninja-debug/sample_cpp_framework_bingo_api" ]]; then
  BIN_DIR="$BIN_DIR/linux-ninja-debug"
fi

API_BIN="$BIN_DIR/sample_cpp_framework_bingo_api"
PLAY_BIN="$BIN_DIR/sample_cpp_framework_bingo_play"
SESSION_BIN="$BIN_DIR/sample_cpp_framework_bingo_session"
CLIENT_BIN="$BIN_DIR/sample_cpp_framework_bingo_client"
CTEST_BIN="${CTEST_BIN:-ctest}"

for binary in "$API_BIN" "$PLAY_BIN" "$SESSION_BIN" "$CLIENT_BIN"; do
  if [[ ! -x "$binary" ]]; then
    echo "Missing executable: $binary" >&2
    echo "CMake build did not produce the expected Bingo sample executable." >&2
    exit 1
  fi
done

"$CTEST_BIN" --test-dir "$BUILD_DIR" \
  -R 'test_cpp_framework_sample_parity|zlink_cpp_framework_mesh_node_vertical_test|test_cpp_framework_actor_gateway' \
  --output-on-failure

read -r -a PORTS <<<"$(python3 - <<'PY'
import random
import socket

sockets = []
try:
    chosen = set()
    blocked = set()
    while len(sockets) < 22:
        port = random.randint(48000, 60999)
        if port in chosen or port in blocked or port + 1000 in chosen or port + 1000 in blocked:
            continue
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        ctrl = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            sock.bind(("127.0.0.1", port))
            ctrl.bind(("127.0.0.1", port + 1000))
        except OSError:
            sock.close()
            ctrl.close()
            continue
        chosen.add(port)
        blocked.add(port + 1000)
        ctrl.close()
        sockets.append(sock)
    print(" ".join(str(sock.getsockname()[1]) for sock in sockets))
finally:
    for sock in sockets:
        sock.close()
PY
  )"

if [[ ${#PORTS[@]} -lt 22 ]]; then
  echo "Failed to allocate 22 local TCP ports for the Bingo sample." >&2
  echo "This environment may block local socket creation." >&2
  exit 1
fi

API_A_CHANNEL_ENDPOINT="tcp://127.0.0.1:${PORTS[2]}"
PLAY_A_CHANNEL_ENDPOINT="tcp://127.0.0.1:${PORTS[3]}"
SESSION_A_SPOT_ENDPOINT="tcp://127.0.0.1:${PORTS[4]}"
SESSION_A_ROUTER_ENDPOINT="tcp://127.0.0.1:${PORTS[5]}"
SESSION_B_SPOT_ENDPOINT="tcp://127.0.0.1:${PORTS[6]}"
SESSION_B_ROUTER_ENDPOINT="tcp://127.0.0.1:${PORTS[7]}"
PLAY_B_CHANNEL_ENDPOINT="tcp://127.0.0.1:${PORTS[8]}"
PLAY_A_SPOT_ENDPOINT="tcp://127.0.0.1:${PORTS[9]}"
PLAY_A_SPOT_ROUTER_ENDPOINT="tcp://127.0.0.1:${PORTS[10]}"
SESSION_A_STREAM_ENDPOINT="tcp://127.0.0.1:${PORTS[11]}"
SESSION_B_STREAM_ENDPOINT="tcp://127.0.0.1:${PORTS[12]}"
PLAY_B_SPOT_ENDPOINT="tcp://127.0.0.1:${PORTS[13]}"
PLAY_B_SPOT_ROUTER_ENDPOINT="tcp://127.0.0.1:${PORTS[14]}"
API_B_CHANNEL_ENDPOINT="tcp://127.0.0.1:${PORTS[15]}"
PLAY_A_ROUTE_ENDPOINT="tcp://127.0.0.1:${PORTS[0]}"
PLAY_B_ROUTE_ENDPOINT="tcp://127.0.0.1:${PORTS[1]}"

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
  for _ in $(seq 1 600); do
    if (echo >"/dev/tcp/${host}/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name} at ${endpoint}" >&2
  return 1
}

wait_log_contains() {
  local description="$1"
  local pattern="$2"
  shift 2
  for _ in $(seq 1 300); do
    if grep -q "$pattern" "$@" 2>/dev/null; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${description}." >&2
  return 1
}

RUN_DIR="$(mktemp -d)"
LOG_DIR="$RUN_DIR/logs"
mkdir -p "$LOG_DIR"
PIDS=()
REDIS_CONTAINER=""
cleanup_done=false
BINGO_REDIS_KEY_PREFIX="bingo:cpp:${RANDOM}:$$:"

cleanup() {
  local code=$?
  set +e
  local cleanup_failed=0
  local status
  if [[ "$cleanup_done" == true ]]; then
    return
  fi
  cleanup_done=true
  for ((i=${#PIDS[@]}-1; i>=0; i--)); do
    local pid="${PIDS[$i]}"
    kill "$pid" 2>/dev/null || true
  done
  for _ in $(seq 1 20); do
    local any_alive=0
    for pid in "${PIDS[@]}"; do
      if kill -0 "$pid" 2>/dev/null; then
        any_alive=1
        break
      fi
    done
    if [[ "$any_alive" == "0" ]]; then
      break
    fi
    sleep 0.1
  done
  for ((i=${#PIDS[@]}-1; i>=0; i--)); do
    local pid="${PIDS[$i]}"
    if kill -0 "$pid" 2>/dev/null; then
      echo "forced cleanup process $pid" >&2
      kill -9 "$pid" 2>/dev/null || true
      cleanup_failed=1
    fi
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
  rm -rf "$RUN_DIR"
  if [[ "$cleanup_failed" -ne 0 && "$code" -eq 0 ]]; then
    code=1
  fi
  return "$code"
}
trap 'cleanup; status=$?; exit "$status"' EXIT

if ! command -v docker >/dev/null 2>&1; then
  echo "Docker is required to run the Bingo sample." >&2
  exit 1
fi
zlink_redis_start_scoped_assign REDIS_CONTAINER redis_port \
  "zlink-redis-cpp-sample-bingo" "redis:7-alpine"
BINGO_REDIS_ENDPOINT="127.0.0.1:${redis_port}"
wait_port redis "tcp://${BINGO_REDIS_ENDPOINT}"

CONFIG_DIR="$LOG_DIR/config"
mkdir -p "$CONFIG_DIR"

# 각 role은 자기 설정 파일 하나만 받는다(공통 정책 sample-e2e-configuration-policy.ko.md §2.1).
write_role_config() {
  python3 - "$CONFIG_DIR/$1.json" "$2" "$3" "$4" "$5" "$6" "$7" "$FLOW_LOG_DIR" \
    "$API_A_CHANNEL_ENDPOINT" "$API_B_CHANNEL_ENDPOINT" "$PLAY_A_CHANNEL_ENDPOINT" \
    "$PLAY_B_CHANNEL_ENDPOINT" "$PLAY_A_ROUTE_ENDPOINT" "$PLAY_B_ROUTE_ENDPOINT" \
    "$PLAY_A_SPOT_ENDPOINT" "$PLAY_B_SPOT_ENDPOINT" "$PLAY_A_SPOT_ROUTER_ENDPOINT" \
    "$PLAY_B_SPOT_ROUTER_ENDPOINT" "$SESSION_A_STREAM_ENDPOINT" \
    "$SESSION_B_STREAM_ENDPOINT" "$BINGO_REDIS_ENDPOINT" "$BINGO_REDIS_KEY_PREFIX" <<'CONFIG_PY'
import json
import os
import stat
import sys

(path, api_node, play_node, session_node, stream_endpoint, session_spot_endpoint,
 session_router_endpoint, flow_log_dir, api_a_channel, api_b_channel,
 play_a_channel, play_b_channel, play_a_route, play_b_route, play_a_spot,
 play_b_spot, play_a_spot_router, play_b_spot_router, session_a_stream,
 session_b_stream, redis_endpoint, redis_key_prefix) = sys.argv[1:]

document = {
    "sample": {
        "host": {"keepRunning": True},
        "topology": {
            "logDir": flow_log_dir,
            "apiNode": api_node,
            "playNode": play_node,
            "sessionNode": session_node,
            "apiChannelEndpoint": api_a_channel,
            "apiAChannelEndpoint": api_a_channel,
            "apiBChannelEndpoint": api_b_channel,
            "playChannelEndpoint": play_a_channel,
            "playAChannelEndpoint": play_a_channel,
            "playBChannelEndpoint": play_b_channel,
            "playARouteEndpoint": play_a_route,
            "playBRouteEndpoint": play_b_route,
            "playASpotEndpoint": play_a_spot,
            "playBSpotEndpoint": play_b_spot,
            "playASpotRouterEndpoint": play_a_spot_router,
            "playBSpotRouterEndpoint": play_b_spot_router,
            "sessionSpotEndpoint": session_spot_endpoint,
            "sessionRouterEndpoint": session_router_endpoint,
            "streamEndpoint": stream_endpoint,
            "sessionAStreamEndpoint": session_a_stream,
            "sessionBStreamEndpoint": session_b_stream,
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

write_role_config play-a a a a "$SESSION_A_STREAM_ENDPOINT" "$SESSION_A_SPOT_ENDPOINT" "$SESSION_A_ROUTER_ENDPOINT"
write_role_config play-b a b a "$SESSION_A_STREAM_ENDPOINT" "$SESSION_A_SPOT_ENDPOINT" "$SESSION_A_ROUTER_ENDPOINT"
write_role_config api-a a a a "$SESSION_A_STREAM_ENDPOINT" "$SESSION_A_SPOT_ENDPOINT" "$SESSION_A_ROUTER_ENDPOINT"
write_role_config api-b b a a "$SESSION_A_STREAM_ENDPOINT" "$SESSION_A_SPOT_ENDPOINT" "$SESSION_A_ROUTER_ENDPOINT"
write_role_config session-a a a a "$SESSION_A_STREAM_ENDPOINT" "$SESSION_A_SPOT_ENDPOINT" "$SESSION_A_ROUTER_ENDPOINT"
write_role_config session-b a a b "$SESSION_B_STREAM_ENDPOINT" "$SESSION_B_SPOT_ENDPOINT" "$SESSION_B_ROUTER_ENDPOINT"

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
start_server session-a "$SESSION_BIN" --config="$CONFIG_DIR/session-a.json"
start_server session-b "$SESSION_BIN" --config="$CONFIG_DIR/session-b.json"

wait_port play-a "$PLAY_A_ROUTE_ENDPOINT"
wait_port play-a-route "$PLAY_A_ROUTE_ENDPOINT"
wait_port play-a-spot-router "$PLAY_A_SPOT_ROUTER_ENDPOINT"
wait_port play-b "$PLAY_B_ROUTE_ENDPOINT"
wait_port play-b-route "$PLAY_B_ROUTE_ENDPOINT"
wait_port play-b-spot-router "$PLAY_B_SPOT_ROUTER_ENDPOINT"
wait_port api-a "$API_A_CHANNEL_ENDPOINT"
wait_port api-b "$API_B_CHANNEL_ENDPOINT"
wait_port session-a-router "$SESSION_A_ROUTER_ENDPOINT"
wait_port session-a-stream "$SESSION_A_STREAM_ENDPOINT"
wait_port session-b-router "$SESSION_B_ROUTER_ENDPOINT"
wait_port session-b-stream "$SESSION_B_STREAM_ENDPOINT"

"$CLIENT_BIN" \
  --session-a-stream-endpoint "$SESSION_A_STREAM_ENDPOINT" \
  --session-b-stream-endpoint "$SESSION_B_STREAM_ENDPOINT" >"$LOG_DIR/client.log" 2>&1 || {
  echo "=== bingo client ===" >&2
  cat "$LOG_DIR/client.log" >&2
  echo "=== bingo session-a ===" >&2
  cat "$LOG_DIR/session-a.log" >&2
  echo "=== bingo session-b ===" >&2
  cat "$LOG_DIR/session-b.log" >&2
  echo "=== bingo play-a ===" >&2
  cat "$LOG_DIR/play-a.log" >&2
  echo "=== bingo play-b ===" >&2
  cat "$LOG_DIR/play-b.log" >&2
  echo "=== bingo api-a ===" >&2
  cat "$LOG_DIR/api-a.log" >&2
  echo "=== bingo api-b ===" >&2
  cat "$LOG_DIR/api-b.log" >&2
  exit 1
}

grep -q "bingo=completed" "$LOG_DIR/client.log"
grep -q "stream-inbound sample=Bingo" "$LOG_DIR/client.log"
grep -Eq "stream-inbound sample=Bingo .* seq=[0-9]" "$LOG_DIR/client.log"
grep -Eq "stream-inbound sample=Bingo .* name=.*Notify" "$LOG_DIR/client.log"
wait_log_contains "player-1 actor destroy completion" \
  "entry spot: actor destroy completed. actor=player-1" "$LOG_DIR"/play-*.log
wait_log_contains "player-2 actor destroy completion" \
  "entry spot: actor destroy completed. actor=player-2" "$LOG_DIR"/play-*.log
wait_log_contains "observer return to Entry Spot" \
  "observer returned to entry spot" "$LOG_DIR"/play-*.log
if grep -Rq "entry spot: actor destroy completed. actor=observer" "$LOG_DIR"/play-*.log; then
  echo "Observer actor must not be destroyed during Bingo player cleanup." >&2
  exit 1
fi
grep -Rq "message flow" "$FLOW_LOG_DIR"
# Bingo §17.2 — the ambient runtime metrics reach the sample's metric log:
# Session sees the STREAM CCU counters, Play sees the room queue instruments.
grep -Rq "zlink.stream.connections.active" "$FLOW_LOG_DIR"/bingo-session-*-metrics.log
grep -Rq "kind=user" "$FLOW_LOG_DIR"/bingo-play-*-metrics.log

cleanup
trap - EXIT

echo "bingo full client/server self-check completed"
