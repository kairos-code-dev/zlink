#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$ROOT_DIR/../redis-common.sh"

if [[ "$#" -eq 0 ]]; then
  SCENARIO="all"
else
  SCENARIO="$*"
  SCENARIO="${SCENARIO// /,}"
fi

RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"

SERVER_PROJECT="$ROOT_DIR/Server/ActorNode/SpotActorTransfer.ActorNode.csproj"
CLIENT_PROJECT="$ROOT_DIR/Client/SpotActorTransfer.Client.csproj"
LOCAL_READINESS_TIMEOUT_SECONDS=30
LOCAL_READINESS_POLL_SECONDS=0.1
REDIS_READINESS_TIMEOUT_SECONDS="${ZLINK_REDIS_READY_TIMEOUT_SECONDS:-60}"
HTTP_PROBE_TIMEOUT_SECONDS=3

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
  sleep 0.5
  for pid in "${pids[@]:-}"; do
    kill -KILL -- "-$pid" >/dev/null 2>&1 || kill -KILL "$pid" >/dev/null 2>&1 || true
  done
  wait "${pids[@]:-}" >/dev/null 2>&1 || true
  if [[ -n "$REDIS_CONTAINER" ]]; then
    docker rm -f "$REDIS_CONTAINER" >/dev/null 2>&1 || true
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
  setsid dotnet run --no-build --project "$SERVER_PROJECT" -- \
    --rid "$rid" \
    --http-url "$url" \
    --redis-endpoint "$REDIS_ENDPOINT" \
    --redis-key-prefix "$REDIS_KEY_PREFIX" \
    --router-endpoint "$router" \
    --evidence-file "$LOG_DIR/${rid}.evidence.log" \
    --log-dir "$LOG_DIR" \
    >"$LOG_DIR/${rid}.stdout.log" 2>"$LOG_DIR/${rid}.stderr.log" &
  pids+=("$!")
}

run_client() {
  local scenario="$1"
  dotnet run --no-build --project "$CLIENT_PROJECT" -- \
    --node-a-url "$NODE_A_URL" \
    --node-b-url "$NODE_B_URL" \
    --scenario "$scenario" \
    >>"$LOG_DIR/client.stdout.log" 2>>"$LOG_DIR/client.stderr.log"
}

zlink_redis_start_scoped_assign \
  REDIS_CONTAINER \
  REDIS_ENDPOINT \
  "zlink-redis-dotnet-e2e-spot-actor-transfer" \
  "redis:7.2-alpine" \
  "$LOG_DIR"
zlink_redis_wait_ready "$REDIS_CONTAINER" "$REDIS_READINESS_TIMEOUT_SECONDS"
REDIS_KEY_PREFIX="zlink:e2e:spot-actor-transfer:$(date +%s)-$$"

NODE_A_HTTP_PORT="$(pick_port)"
NODE_B_HTTP_PORT="$(pick_port)"
NODE_A_ROUTER_PORT="$(pick_port)"
NODE_B_ROUTER_PORT="$(pick_port)"
NODE_A_URL="http://127.0.0.1:$NODE_A_HTTP_PORT"
NODE_B_URL="http://127.0.0.1:$NODE_B_HTTP_PORT"
NODE_A_ROUTER="tcp://127.0.0.1:$NODE_A_ROUTER_PORT"
NODE_B_ROUTER="tcp://127.0.0.1:$NODE_B_ROUTER_PORT"

echo "log_dir=$LOG_DIR"
dotnet build "$SERVER_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$CLIENT_PROJECT" --maxcpucount:1 >/dev/null

start_node actor-a "$NODE_A_URL" "$NODE_A_ROUTER"
start_node actor-b "$NODE_B_URL" "$NODE_B_ROUTER"

wait_health "$NODE_A_URL" actor-a
wait_health "$NODE_B_URL" actor-b
sleep 5

: >"$LOG_DIR/client.stdout.log"
: >"$LOG_DIR/client.stderr.log"

if [[ "$SCENARIO" == "all" ]]; then
  run_client "ST-A1,ST-A2,ST-A3,ST-B1,ST-B3,ST-B4,ST-D1,ST-C3"
  run_client "ST-C2"
  sleep 1
  NODE_A_HTTP_PORT="$(pick_port)"
  NODE_A_ROUTER_PORT="$(pick_port)"
  NODE_A_URL="http://127.0.0.1:$NODE_A_HTTP_PORT"
  NODE_A_ROUTER="tcp://127.0.0.1:$NODE_A_ROUTER_PORT"
  start_node actor-a "$NODE_A_URL" "$NODE_A_ROUTER"
  wait_health "$NODE_A_URL" actor-a
  sleep 5
  run_client "ST-C1"
else
  run_client "$SCENARIO"
fi

cat "$LOG_DIR/client.stdout.log"
