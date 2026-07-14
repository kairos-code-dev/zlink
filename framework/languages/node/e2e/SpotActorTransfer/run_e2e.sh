#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NODE_ROOT="$(cd "$ROOT_DIR/../.." && pwd)"
source "$NODE_ROOT/e2e/redis-container.sh"

SCENARIO="${1:-all}"
CHILD_RUN="${2:-}"
if [[ "$CHILD_RUN" != "--child-run" && "$SCENARIO" == "all" ]]; then
  for child_scenario in all-core ST-F3; do
    "$0" "$child_scenario" --child-run
  done
  echo "spot-actor-transfer e2e result=passed"
  exit 0
fi
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/log/$RUN_ID"
mkdir -p "$LOG_DIR"

wait_health() {
  local url="$1" name="$2" pid="${3:-}"
  for _ in $(seq 1 160); do
    if [[ -n "$pid" ]] && ! process_alive "$pid"; then
      set +e
      wait "$pid"
      local status=$?
      set -e
      echo "$name exited before readiness with status $status" >&2
      return 1
    fi
    if curl -fsS "$url/health" >/dev/null 2>&1; then return 0; fi
    sleep 0.25
  done
  echo "Timed out waiting for $name at $url" >&2
  return 1
}

process_alive() {
  local pid="$1" state
  kill -0 "$pid" 2>/dev/null || return 1
  state="$(ps -o stat= -p "$pid" 2>/dev/null || true)"
  [[ -n "$state" && "$state" != Z* ]]
}

wait_tcp() {
  local name="$1" endpoint="$2" host_port="${2#*://}"
  local host="${host_port%:*}" port="${host_port##*:}"
  for _ in $(seq 1 120); do
    if node -e "const net=require('node:net');const s=net.createConnection({host:process.argv[1],port:Number(process.argv[2])});s.once('connect',()=>{s.end();process.exit(0)});s.once('error',()=>process.exit(1));setTimeout(()=>process.exit(1),500)" "$host" "$port"; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for $name at $endpoint" >&2
  return 1
}

wait_topology() {
  node "$NODE_ROOT/e2e/location-readiness.js" \
    --redis-endpoint "$REDIS_ENDPOINT" \
    --key-prefix "$REDIS_KEY_PREFIX" \
    --peer spot-mesh spot-actor-transfer spot \
      "$NODE_A_ROUTER" "$NODE_A_PUBSUB" \
      "$NODE_B_ROUTER" "$NODE_B_PUBSUB" \
      "$SESSION_A_ROUTER" "$SESSION_A_PUBSUB" \
      "$SESSION_B_ROUTER" "$SESSION_B_PUBSUB"
}

pids=()
REDIS_CONTAINER_ID=""
cleanup() {
  local code=$?
  for pid in "${pids[@]:-}"; do kill "$pid" >/dev/null 2>&1 || true; done
  for _ in $(seq 1 40); do
    local any_alive=0
    for pid in "${pids[@]:-}"; do
      if process_alive "$pid"; then
        any_alive=1
        break
      fi
    done
    [[ "$any_alive" == "0" ]] && break
    sleep 0.05
  done
  for pid in "${pids[@]:-}"; do kill -KILL "$pid" >/dev/null 2>&1 || true; done
  wait "${pids[@]:-}" >/dev/null 2>&1 || true
  if [[ -n "$REDIS_CONTAINER_ID" ]]; then docker rm -fv "$REDIS_CONTAINER_ID" >/dev/null 2>&1 || true; fi
  if [[ "$code" -ne 0 ]]; then
    echo "E2E failed. log_dir=$LOG_DIR" >&2
    for file in "$LOG_DIR"/*.stderr.log; do
      [[ -f "$file" ]] || continue
      echo "----- $file -----" >&2
      tail -n 100 "$file" >&2 || true
    done
  fi
}
trap cleanup EXIT

start_node() {
  local rid="$1" url="$2" router="$3" pubsub="$4"
  local config="$LOG_DIR/$rid.config.json"
  node "$ROOT_DIR/write-config.mjs" "$config" \
    --rid "$rid" \
    --http-url "$url" \
    --redis-endpoint "$REDIS_ENDPOINT" \
    --redis-key-prefix "$REDIS_KEY_PREFIX" \
    --router-endpoint "$router" \
    --pubsub-endpoint "$pubsub" \
    --evidence-file "$LOG_DIR/$rid.evidence.log" \
    --log-dir "$LOG_DIR"
  node "$ACTOR_NODE_MAIN" --config "$config" \
    >>"$LOG_DIR/$rid.stdout.log" 2>>"$LOG_DIR/$rid.stderr.log" &
  pids+=("$!")
}

start_session() {
  local rid="$1" url="$2" router="$3" pubsub="$4" stream="$5"
  local config="$LOG_DIR/$rid.config.json"
  node "$ROOT_DIR/write-config.mjs" "$config" \
    --rid "$rid" \
    --http-url "$url" \
    --redis-endpoint "$REDIS_ENDPOINT" \
    --redis-key-prefix "$REDIS_KEY_PREFIX" \
    --router-endpoint "$router" \
    --pubsub-endpoint "$pubsub" \
    --stream-endpoint "$stream" \
    --evidence-file "$LOG_DIR/$rid.evidence.log" \
    --log-dir "$LOG_DIR"
  node "$SESSION_MAIN" --config "$config" \
    >"$LOG_DIR/$rid.stdout.log" 2>"$LOG_DIR/$rid.stderr.log" &
  pids+=("$!")
  wait_health "$url" "$rid" "${pids[-1]}"
  wait_tcp "$rid-stream" "$stream"
}

start_node_a() {
  start_node actor-a "$NODE_A_URL" "$NODE_A_ROUTER" "$NODE_A_PUBSUB"
  NODE_A_PID="${pids[-1]}"
  wait_health "$NODE_A_URL" actor-a "${pids[-1]}"
  sleep 2
}

run_client() {
  local scenario="$1"
  node "$NODE_ROOT/scripts/browser-e2e/run-e2e-client.mjs" "$ROOT_DIR/Client/main.ts" -- \
    --node-a-url "$NODE_A_URL" \
    --node-b-url "$NODE_B_URL" \
    --session-a-stream-endpoint "$SESSION_A_STREAM" \
    --session-b-stream-endpoint "$SESSION_B_STREAM" \
    --scenario "$scenario" \
    >>"$LOG_DIR/client.stdout.log" 2>>"$LOG_DIR/client.stderr.log"
}

restart_node_a() {
  if [[ -n "${NODE_A_PID:-}" ]]; then
    kill -TERM "$NODE_A_PID" 2>/dev/null || true
    for _ in {1..50}; do
      kill -0 "$NODE_A_PID" 2>/dev/null || break
      sleep 0.1
    done
    kill -KILL "$NODE_A_PID" 2>/dev/null || true
    wait "$NODE_A_PID" || true
  fi
  start_node_a
}

echo "log_dir=$LOG_DIR"
(cd "$NODE_ROOT" && npm run build >/dev/null)
(cd "$ROOT_DIR/Server/ActorNode" && npm run build >/dev/null)
(cd "$ROOT_DIR/Server/Session" && npm run build >/dev/null)
(cd "$ROOT_DIR/Client" && npm run build >/dev/null)

if ! command -v docker >/dev/null 2>&1; then
  echo "Docker is required to run SpotActorTransfer." >&2
  exit 1
fi
start_redis_container "zlink-redis-node-spot-transfer-${RANDOM}-$$" -p "127.0.0.1::6379" "redis:7.2-alpine"
REDIS_ENDPOINT="$(redis_container_endpoint "$REDIS_CONTAINER_ID")"
wait_tcp redis "tcp://$REDIS_ENDPOINT"
REDIS_KEY_PREFIX="spot-actor-transfer:node:$RUN_ID"

read -r -a PORTS <<<"$(python3 - <<'PY'
import random
import socket

sockets = []
try:
    chosen = set()
    while len(sockets) < 14:
        port = random.randint(20000, 60999)
        if port in chosen:
            continue
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            sock.bind(('127.0.0.1', port))
        except OSError:
            sock.close()
            continue
        chosen.add(port)
        sockets.append(sock)
    print(' '.join(str(sock.getsockname()[1]) for sock in sockets))
finally:
    for sock in sockets:
        sock.close()
PY
)"
NODE_A_HTTP_PORT="${PORTS[0]}"
NODE_B_HTTP_PORT="${PORTS[1]}"
NODE_A_ROUTER_PORT="${PORTS[2]}"
NODE_B_ROUTER_PORT="${PORTS[3]}"
NODE_A_PUBSUB_PORT="${PORTS[4]}"
NODE_B_PUBSUB_PORT="${PORTS[5]}"
SESSION_A_HTTP_PORT="${PORTS[6]}"
SESSION_B_HTTP_PORT="${PORTS[7]}"
SESSION_A_ROUTER_PORT="${PORTS[8]}"
SESSION_B_ROUTER_PORT="${PORTS[9]}"
SESSION_A_PUBSUB_PORT="${PORTS[10]}"
SESSION_B_PUBSUB_PORT="${PORTS[11]}"
SESSION_A_STREAM_PORT="${PORTS[12]}"
SESSION_B_STREAM_PORT="${PORTS[13]}"
NODE_A_URL="http://127.0.0.1:$NODE_A_HTTP_PORT"
NODE_B_URL="http://127.0.0.1:$NODE_B_HTTP_PORT"
NODE_A_ROUTER="tcp://127.0.0.1:$NODE_A_ROUTER_PORT"
NODE_B_ROUTER="tcp://127.0.0.1:$NODE_B_ROUTER_PORT"
NODE_A_PUBSUB="tcp://127.0.0.1:$NODE_A_PUBSUB_PORT"
NODE_B_PUBSUB="tcp://127.0.0.1:$NODE_B_PUBSUB_PORT"
SESSION_A_URL="http://127.0.0.1:$SESSION_A_HTTP_PORT"
SESSION_B_URL="http://127.0.0.1:$SESSION_B_HTTP_PORT"
SESSION_A_ROUTER="tcp://127.0.0.1:$SESSION_A_ROUTER_PORT"
SESSION_B_ROUTER="tcp://127.0.0.1:$SESSION_B_ROUTER_PORT"
SESSION_A_PUBSUB="tcp://127.0.0.1:$SESSION_A_PUBSUB_PORT"
SESSION_B_PUBSUB="tcp://127.0.0.1:$SESSION_B_PUBSUB_PORT"
SESSION_A_STREAM="ws://127.0.0.1:$SESSION_A_STREAM_PORT"
SESSION_B_STREAM="ws://127.0.0.1:$SESSION_B_STREAM_PORT"
ACTOR_NODE_MAIN="$ROOT_DIR/Server/ActorNode/dist/Server/ActorNode/main.js"
SESSION_MAIN="$ROOT_DIR/Server/Session/dist/Server/Session/main.js"

start_node actor-b "$NODE_B_URL" "$NODE_B_ROUTER" "$NODE_B_PUBSUB"
wait_health "$NODE_B_URL" actor-b "${pids[-1]}"
start_node_a
start_session session-a "$SESSION_A_URL" "$SESSION_A_ROUTER" "$SESSION_A_PUBSUB" "$SESSION_A_STREAM"
start_session session-b "$SESSION_B_URL" "$SESSION_B_ROUTER" "$SESSION_B_PUBSUB" "$SESSION_B_STREAM"
wait_topology

: >"$LOG_DIR/client.stdout.log"
: >"$LOG_DIR/client.stderr.log"

if [[ "$SCENARIO" == "all-core" ]]; then
  run_client "ST-A1,ST-A2,ST-A3,ST-B1,ST-B3,ST-B4,ST-C3,ST-D1,ST-D2,ST-E1,ST-E2,ST-F1,ST-F2,ST-F4,ST-F5,ST-F6"
  run_client "ST-C2"
  restart_node_a
  run_client "ST-B2"
  restart_node_a
  run_client "ST-C1"
else
  run_client "$SCENARIO"
fi

cat "$LOG_DIR/client.stdout.log"
