#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$ROOT_DIR/../redis-common.sh"
CPP_ROOT="$(cd "$ROOT_DIR/../.." && pwd)"
BUILD_DIR="${ZLINK_CPP_BUILD_DIR:-$CPP_ROOT/build}"

if [[ "$#" -eq 0 ]]; then
  SCENARIO="all"
else
  SCENARIO="$*"
  SCENARIO="${SCENARIO// /,}"
fi

RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"

NODE_BIN="$BUILD_DIR/zlink_cpp_e2e_spot_actor_transfer_node"
CLIENT_BIN="$BUILD_DIR/zlink_cpp_e2e_spot_actor_transfer_client"
LOCAL_READINESS_TIMEOUT_SECONDS=3
LOCAL_READINESS_POLL_SECONDS=0.1
ROUTE_SETTLE_SECONDS=5
SCENARIO_SETTLE_SECONDS=3
REDIS_READINESS_TIMEOUT_SECONDS="${ZLINK_REDIS_READY_TIMEOUT_SECONDS:-60}"
HTTP_PROBE_TIMEOUT_SECONDS=3
SHUTDOWN_GRACE_SECONDS=0.5

pick_port() {
  python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
}

pids=()
REDIS_CONTAINER=""

cleanup() {
  local code=$?
  for pid in "${pids[@]:-}"; do
    kill -- "-$pid" >/dev/null 2>&1 || kill "$pid" >/dev/null 2>&1 || true
  done
  sleep "$SHUTDOWN_GRACE_SECONDS"
  for pid in "${pids[@]:-}"; do
    kill -KILL -- "-$pid" >/dev/null 2>&1 || kill -KILL "$pid" >/dev/null 2>&1 || true
  done
  wait "${pids[@]:-}" >/dev/null 2>&1 || true
  if [[ -n "$REDIS_CONTAINER" ]]; then
    docker rm -fv "$REDIS_CONTAINER" >/dev/null 2>&1 || true
  fi
  if [[ "$code" -ne 0 ]]; then
    echo "E2E failed. Logs: $LOG_DIR" >&2
  fi
}
trap cleanup EXIT

wait_health() {
  local url="$1"
  local name="$2"
  local attempts
  attempts="$(
    python3 - "$LOCAL_READINESS_TIMEOUT_SECONDS" "$LOCAL_READINESS_POLL_SECONDS" <<'PY'
import math
import sys
print(max(1, math.ceil(float(sys.argv[1]) / float(sys.argv[2]))))
PY
  )"
  for _ in $(seq 1 "$attempts"); do
    if curl --max-time "$HTTP_PROBE_TIMEOUT_SECONDS" \
      --connect-timeout "$HTTP_PROBE_TIMEOUT_SECONDS" \
      -fsS "$url/health" >/dev/null 2>&1; then
      return 0
    fi
    sleep "$LOCAL_READINESS_POLL_SECONDS"
  done
  echo "Timed out waiting ${LOCAL_READINESS_TIMEOUT_SECONDS}s for $name at $url" >&2
  return 1
}

start_node() {
  local rid="$1"
  local url="$2"
  local router="$3"
  local stream="$4"
  local pub="$5"
  ZLINK_CPP_E2E_ACTOR_RID="$rid" \
  ZLINK_CPP_E2E_ACTOR_HTTP="$url" \
  ZLINK_CPP_E2E_ACTOR_ROUTER="$router" \
  ZLINK_CPP_E2E_ACTOR_STREAM="$stream" \
  ZLINK_CPP_E2E_ACTOR_PUB="$pub" \
  ZLINK_CPP_E2E_REDIS_LOCATION_ENDPOINT="$REDIS_ENDPOINT" \
  ZLINK_CPP_E2E_LOCATION_KEY_PREFIX="$REDIS_KEY_PREFIX" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
  ZLINK_CPP_E2E_EVIDENCE_FILE="$LOG_DIR/${rid}.evidence.log" \
  setsid "$NODE_BIN" \
    >"$LOG_DIR/${rid}.stdout.log" 2>"$LOG_DIR/${rid}.stderr.log" &
  pids+=("$!")
}

run_client() {
  local scenario="$1"
  ZLINK_CPP_E2E_NODE_A_URL="$NODE_A_URL" \
  ZLINK_CPP_E2E_NODE_B_URL="$NODE_B_URL" \
  ZLINK_CPP_E2E_NODE_C_URL="$NODE_C_URL" \
  ZLINK_CPP_E2E_NODE_A_STREAM="$NODE_A_STREAM" \
  ZLINK_CPP_E2E_NODE_B_STREAM="$NODE_B_STREAM" \
  ZLINK_CPP_E2E_SCENARIO="$scenario" \
  "$CLIENT_BIN" \
    >>"$LOG_DIR/client.stdout.log" 2>>"$LOG_DIR/client.stderr.log"
}

zlink_redis_start_scoped_assign REDIS_CONTAINER redis_port \
  "zlink-redis-cpp-e2e-spot-actor-transfer" "redis:7-alpine"
REDIS_ENDPOINT="127.0.0.1:${redis_port}"
zlink_redis_wait_ready "$REDIS_CONTAINER" "$REDIS_READINESS_TIMEOUT_SECONDS"
REDIS_KEY_PREFIX="zlink:cpp-e2e:spot-actor-transfer:$(date +%s)-$$"

NODE_A_HTTP_PORT="$(pick_port)"
NODE_B_HTTP_PORT="$(pick_port)"
NODE_C_HTTP_PORT="$(pick_port)"
NODE_A_ROUTER_PORT="$(pick_port)"
NODE_B_ROUTER_PORT="$(pick_port)"
NODE_C_ROUTER_PORT="$(pick_port)"
NODE_A_STREAM_PORT="$(pick_port)"
NODE_B_STREAM_PORT="$(pick_port)"
NODE_C_STREAM_PORT="$(pick_port)"
NODE_A_PUB_PORT="$(pick_port)"
NODE_B_PUB_PORT="$(pick_port)"
NODE_C_PUB_PORT="$(pick_port)"
NODE_A_PUB="tcp://127.0.0.1:$NODE_A_PUB_PORT"
NODE_B_PUB="tcp://127.0.0.1:$NODE_B_PUB_PORT"
NODE_C_PUB="tcp://127.0.0.1:$NODE_C_PUB_PORT"
NODE_A_URL="http://127.0.0.1:$NODE_A_HTTP_PORT"
NODE_B_URL="http://127.0.0.1:$NODE_B_HTTP_PORT"
NODE_C_URL="http://127.0.0.1:$NODE_C_HTTP_PORT"
NODE_A_ROUTER="tcp://127.0.0.1:$NODE_A_ROUTER_PORT"
NODE_B_ROUTER="tcp://127.0.0.1:$NODE_B_ROUTER_PORT"
NODE_C_ROUTER="tcp://127.0.0.1:$NODE_C_ROUTER_PORT"
NODE_A_STREAM="tcp://127.0.0.1:$NODE_A_STREAM_PORT"
NODE_B_STREAM="tcp://127.0.0.1:$NODE_B_STREAM_PORT"
NODE_C_STREAM="tcp://127.0.0.1:$NODE_C_STREAM_PORT"

echo "log_dir=$LOG_DIR"

start_node actor-a "$NODE_A_URL" "$NODE_A_ROUTER" "$NODE_A_STREAM" "$NODE_A_PUB"
start_node actor-b "$NODE_B_URL" "$NODE_B_ROUTER" "$NODE_B_STREAM" "$NODE_B_PUB"
start_node actor-c "$NODE_C_URL" "$NODE_C_ROUTER" "$NODE_C_STREAM" "$NODE_C_PUB"

wait_health "$NODE_A_URL" actor-a
wait_health "$NODE_B_URL" actor-b
wait_health "$NODE_C_URL" actor-c
sleep "$ROUTE_SETTLE_SECONDS"

: >"$LOG_DIR/client.stdout.log"
: >"$LOG_DIR/client.stderr.log"

restart_node_a() {
  NODE_A_HTTP_PORT="$(pick_port)"
  NODE_A_ROUTER_PORT="$(pick_port)"
  NODE_A_STREAM_PORT="$(pick_port)"
  NODE_A_PUB_PORT="$(pick_port)"
  NODE_A_URL="http://127.0.0.1:$NODE_A_HTTP_PORT"
  NODE_A_ROUTER="tcp://127.0.0.1:$NODE_A_ROUTER_PORT"
  NODE_A_STREAM="tcp://127.0.0.1:$NODE_A_STREAM_PORT"
  NODE_A_PUB="tcp://127.0.0.1:$NODE_A_PUB_PORT"
  start_node actor-a "$NODE_A_URL" "$NODE_A_ROUTER" "$NODE_A_STREAM" "$NODE_A_PUB"
  wait_health "$NODE_A_URL" actor-a
  sleep "$ROUTE_SETTLE_SECONDS"
}

if [[ "$SCENARIO" == "all" ]]; then
  run_client "ST-A1,ST-A2,ST-A3,ST-B1,ST-B3,ST-B4,ST-D1,ST-C3,ST-D2,ST-E1,ST-E2,ST-F1,ST-F2,ST-F3,ST-F4,ST-F5"
  # ST-F6 exercises long in-flight delays (gate hold + 1s late-reply handler);
  # run it in a fresh client process, like the shutdown scenarios below.
  run_client "ST-F6"
  run_client "ST-C2"
  sleep "$SCENARIO_SETTLE_SECONDS"
  restart_node_a
  run_client "ST-B2"
  sleep "$SCENARIO_SETTLE_SECONDS"
  restart_node_a
  run_client "ST-C1"
else
  run_client "$SCENARIO"
fi

cat "$LOG_DIR/client.stdout.log"
