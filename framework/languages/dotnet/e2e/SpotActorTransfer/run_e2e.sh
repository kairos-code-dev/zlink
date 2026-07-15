#!/usr/bin/env bash
set -euo pipefail
umask 077

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
CONFIG_DIR="$(mktemp -d)"

SERVER_PROJECT="$ROOT_DIR/Server/ActorNode/SpotActorTransfer.ActorNode.csproj"
SESSION_GATEWAY_PROJECT="$ROOT_DIR/Server/SessionGateway/SpotActorTransfer.SessionGateway.csproj"
CLIENT_PROJECT="$ROOT_DIR/Client/SpotActorTransfer.Client.csproj"
LOCAL_READINESS_TIMEOUT_SECONDS=3
PROCESS_EXIT_TIMEOUT_SECONDS=30
LOCAL_READINESS_POLL_SECONDS=0.1
REDIS_READINESS_TIMEOUT_SECONDS=60
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
  rm -rf "$CONFIG_DIR"
  for pid in "${pids[@]:-}"; do
    kill -- "-$pid" >/dev/null 2>&1 || kill "$pid" >/dev/null 2>&1 || true
  done
  sleep 0.5
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

wait_process_exit() {
  local pid="$1"
  local name="$2"
  local attempts=$((PROCESS_EXIT_TIMEOUT_SECONDS * 10))
  for _ in $(seq 1 "$attempts"); do
    if ! kill -0 "$pid" >/dev/null 2>&1; then
      wait "$pid" >/dev/null 2>&1 || true
      return 0
    fi
    sleep "$LOCAL_READINESS_POLL_SECONDS"
  done
  echo "Timed out waiting ${PROCESS_EXIT_TIMEOUT_SECONDS}s for $name process $pid to exit" >&2
  return 1
}

start_node() {
  local rid="$1"
  local url="$2"
  local router="$3"
  local config="$CONFIG_DIR/$rid.json"
  python3 "$ROOT_DIR/../write_role_config.py" "$config" -- \
    --rid "$rid" \
    --http-url "$url" \
    --redis-endpoint "$REDIS_ENDPOINT" \
    --redis-key-prefix "$REDIS_KEY_PREFIX" \
    --router-endpoint "$router" \
    --evidence-file "$LOG_DIR/${rid}.evidence.log" \
    --log-dir "$LOG_DIR"
  ZLINK_DEBUG_FRAMEWORK_SPOT_DISCOVERY=1 \
    setsid dotnet run --no-build --project "$SERVER_PROJECT" -- --config "$config" \
    >>"$LOG_DIR/${rid}.stdout.log" 2>>"$LOG_DIR/${rid}.stderr.log" &
  pids+=("$!")
}

start_session_gateway() {
  local rid="$1" url="$2" router="$3" stream="$4"
  local config="$CONFIG_DIR/$rid.json"
  python3 "$ROOT_DIR/../write_role_config.py" "$config" -- \
    --rid "$rid" \
    --http-url "$url" \
    --redis-endpoint "$REDIS_ENDPOINT" \
    --redis-key-prefix "$REDIS_KEY_PREFIX" \
    --router-endpoint "$router" \
    --stream-endpoint "$stream" \
    --evidence-file "$LOG_DIR/${rid}.evidence.log"
  setsid dotnet run --no-build --project "$SESSION_GATEWAY_PROJECT" -- --config "$config" \
    >>"$LOG_DIR/${rid}.stdout.log" 2>>"$LOG_DIR/${rid}.stderr.log" &
  pids+=("$!")
}

run_client() {
  local scenario="$1"
  local config="$CONFIG_DIR/client-$scenario.json"
  python3 "$ROOT_DIR/../write_role_config.py" "$config" -- \
    --config-dir "$CONFIG_DIR" \
    --node-a-url "$NODE_A_URL" \
    --node-b-url "$NODE_B_URL" \
    --node-c-url "$NODE_C_URL" \
    --node-a-stream-endpoint "$SESSION_A_STREAM" \
    --node-b-stream-endpoint "$SESSION_B_STREAM" \
    --scenario "$scenario"
  dotnet run --no-build --project "$CLIENT_PROJECT" -- --config "$config" \
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
NODE_C_HTTP_PORT="$(pick_port)"
NODE_A_ROUTER_PORT="$(pick_port)"
NODE_B_ROUTER_PORT="$(pick_port)"
NODE_C_ROUTER_PORT="$(pick_port)"
SESSION_A_HTTP_PORT="$(pick_port)"
SESSION_B_HTTP_PORT="$(pick_port)"
SESSION_A_ROUTER_PORT="$(pick_port)"
SESSION_B_ROUTER_PORT="$(pick_port)"
SESSION_A_STREAM_PORT="$(pick_port)"
SESSION_B_STREAM_PORT="$(pick_port)"
NODE_A_URL="http://127.0.0.1:$NODE_A_HTTP_PORT"
NODE_B_URL="http://127.0.0.1:$NODE_B_HTTP_PORT"
NODE_C_URL="http://127.0.0.1:$NODE_C_HTTP_PORT"
NODE_A_ROUTER="tcp://127.0.0.1:$NODE_A_ROUTER_PORT"
NODE_B_ROUTER="tcp://127.0.0.1:$NODE_B_ROUTER_PORT"
NODE_C_ROUTER="tcp://127.0.0.1:$NODE_C_ROUTER_PORT"
SESSION_A_URL="http://127.0.0.1:$SESSION_A_HTTP_PORT"
SESSION_B_URL="http://127.0.0.1:$SESSION_B_HTTP_PORT"
SESSION_A_ROUTER="tcp://127.0.0.1:$SESSION_A_ROUTER_PORT"
SESSION_B_ROUTER="tcp://127.0.0.1:$SESSION_B_ROUTER_PORT"
SESSION_A_STREAM="tcp://127.0.0.1:$SESSION_A_STREAM_PORT"
SESSION_B_STREAM="tcp://127.0.0.1:$SESSION_B_STREAM_PORT"

echo "log_dir=$LOG_DIR"
dotnet build "$SERVER_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$SESSION_GATEWAY_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$CLIENT_PROJECT" --maxcpucount:1 >/dev/null

start_node actor-a "$NODE_A_URL" "$NODE_A_ROUTER"
NODE_A_PID="${pids[${#pids[@]}-1]}"
start_node actor-b "$NODE_B_URL" "$NODE_B_ROUTER"
start_node actor-c "$NODE_C_URL" "$NODE_C_ROUTER"

wait_health "$NODE_A_URL" actor-a
wait_health "$NODE_B_URL" actor-b
wait_health "$NODE_C_URL" actor-c
start_session_gateway session-a "$SESSION_A_URL" "$SESSION_A_ROUTER" "$SESSION_A_STREAM"
wait_health "$SESSION_A_URL" session-a
start_session_gateway session-b "$SESSION_B_URL" "$SESSION_B_ROUTER" "$SESSION_B_STREAM"
wait_health "$SESSION_B_URL" session-b

: >"$LOG_DIR/client.stdout.log"
: >"$LOG_DIR/client.stderr.log"

if [[ "$SCENARIO" == "all" ]]; then
  run_client "ST-A1,ST-A2,ST-A3,ST-B1,ST-B3,ST-B4,ST-D1,ST-C3,ST-D2,ST-E1,ST-E2,ST-F1,ST-F2,ST-F3,ST-F4,ST-F5,ST-F6"
  run_client "ST-B2"
  wait_process_exit "$NODE_A_PID" actor-a
  NODE_A_HTTP_PORT="$(pick_port)"
  NODE_A_ROUTER_PORT="$(pick_port)"
  NODE_A_URL="http://127.0.0.1:$NODE_A_HTTP_PORT"
  NODE_A_ROUTER="tcp://127.0.0.1:$NODE_A_ROUTER_PORT"
  start_node actor-a "$NODE_A_URL" "$NODE_A_ROUTER"
  NODE_A_PID="${pids[${#pids[@]}-1]}"
  wait_health "$NODE_A_URL" actor-a
  run_client "ST-C2"
  wait_process_exit "$NODE_A_PID" actor-a
  NODE_A_HTTP_PORT="$(pick_port)"
  NODE_A_ROUTER_PORT="$(pick_port)"
  NODE_A_URL="http://127.0.0.1:$NODE_A_HTTP_PORT"
  NODE_A_ROUTER="tcp://127.0.0.1:$NODE_A_ROUTER_PORT"
  start_node actor-a "$NODE_A_URL" "$NODE_A_ROUTER"
  NODE_A_PID="${pids[${#pids[@]}-1]}"
  wait_health "$NODE_A_URL" actor-a
  run_client "ST-C1"
else
  run_client "$SCENARIO"
fi

require_runtime_marker() {
  local marker="$1"
  if ! grep -h -q "$marker" "$LOG_DIR"/actor-*.stderr.log; then
    echo "Missing runtime marker '$marker'. Logs: $LOG_DIR" >&2
    return 1
  fi
}

require_marker_order() {
  local actor_prefix="$1"
  local first="$2"
  local second="$3"
  python3 - "$LOG_DIR" "$actor_prefix" "$first" "$second" <<'PY'
import pathlib
import re
import sys

log_dir, actor_prefix, first, second = sys.argv[1:]
lines = []
for path in pathlib.Path(log_dir).glob("actor-*.stderr.log"):
    lines.extend(path.read_text().splitlines())
actor_pattern = re.compile(rf"actor=({re.escape(actor_prefix)}[^ ]+)")
actors = {m.group(1) for line in lines if (m := actor_pattern.search(line))}
if len(actors) != 1:
    raise SystemExit(f"Expected one actor for prefix {actor_prefix}, got {sorted(actors)}")
actor = next(iter(actors))
first_index = next((i for i, line in enumerate(lines) if first in line and f"actor={actor}" in line), None)
second_index = next((i for i, line in enumerate(lines) if second in line and f"actor={actor}" in line), None)
if first_index is None or second_index is None or first_index >= second_index:
    raise SystemExit(f"Marker order failed for {actor}: {first}={first_index}, {second}={second_index}")
PY
}

if [[ "$SCENARIO" == "all" || "$SCENARIO" == *"ST-F1"* ]]; then
  require_runtime_marker handoff_backlog
  require_runtime_marker backlog_enqueued
fi
if [[ "$SCENARIO" == "all" || "$SCENARIO" == *"ST-F2"* ]]; then
  require_marker_order actor-inflight-overtake- backlog_enqueued location_committed
fi
if [[ "$SCENARIO" == "all" || "$SCENARIO" == *"ST-F4"* ]]; then
  require_runtime_marker straggler_forward
  require_runtime_marker stale_fail_fast
fi
if [[ "$SCENARIO" == "all" || "$SCENARIO" == *"ST-F5"* ]]; then
  require_runtime_marker mapping_evicted
  grep -h -E -q 'mapping_installed actor=actor-map-chain-.* source=actor-a target=actor-b entries=1' "$LOG_DIR"/actor-a.stderr.log
  grep -h -E -q 'mapping_installed actor=actor-map-chain-.* source=actor-b target=actor-c entries=1' "$LOG_DIR"/actor-b.stderr.log
  grep -h -E -q 'mapping_evicted actor=actor-map-chain-.* entries=0' "$LOG_DIR"/actor-a.stderr.log
  grep -h -E -q 'mapping_evicted actor=actor-map-chain-.* entries=0' "$LOG_DIR"/actor-b.stderr.log
fi
if [[ "$SCENARIO" == "all" || "$SCENARIO" == *"ST-F6"* ]]; then
  grep -h -E -q 'handoff_backlog actor=actor-inflight-req-.* kind=Request request_id=[1-9][0-9]* flags=[1-9][0-9]*' "$LOG_DIR"/actor-a.stderr.log
  grep -h -E -q 'backlog_enqueued actor=actor-inflight-req-.* request_id=[1-9][0-9]* flags=[1-9][0-9]*' "$LOG_DIR"/actor-b.stderr.log
  grep -h -E -q 'request_reply_direct actor=actor-inflight-req-' "$LOG_DIR"/actor-b.stderr.log
fi

cat "$LOG_DIR/client.stdout.log"
