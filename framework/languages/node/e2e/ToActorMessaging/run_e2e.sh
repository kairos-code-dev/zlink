#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NODE_ROOT="$(cd "$ROOT_DIR/../.." && pwd)"
source "$NODE_ROOT/e2e/redis-container.sh"
source "$NODE_ROOT/e2e/runner-common.sh"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
SCENARIO="${1:-all}"
E2E_START_ORDER="${E2E_START_ORDER:-${2:-forward}}"
mkdir -p "$LOG_DIR"

pick_port() {
  node "$NODE_ROOT/e2e/port-picker.js"
}

wait_health() {
  local url="$1"
  local name="$2"
  for _ in $(seq 1 120); do
    if curl -fsS "$url/health" >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.25
  done
  echo "Timed out waiting for $name at $url" >&2
  return 1
}

wait_tcp() {
  local name="$1"
  local endpoint="$2"
  local host_port="${endpoint#*://}"
  local host="${host_port%:*}"
  local port="${host_port##*:}"
  for _ in $(seq 1 100); do
    if node -e "const net=require('node:net'); const s=net.createConnection({host: process.argv[1], port: Number(process.argv[2])}); s.once('connect', () => { s.end(); process.exit(0); }); s.once('error', () => process.exit(1)); setTimeout(() => process.exit(1), 500);" "$host" "$port"; then
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
    --peer spot-mesh to-actor spot \
      "tcp://127.0.0.1:$ACTOR_ROUTER_PORT" \
      "tcp://127.0.0.1:$ACTOR_PUBSUB_PORT" \
      "tcp://127.0.0.1:$SESSION_ROUTER_PORT" \
      "tcp://127.0.0.1:$SESSION_PUBSUB_PORT" \
      "tcp://127.0.0.1:$CALLER_ROUTER_PORT" \
      "tcp://127.0.0.1:$CALLER_PUBSUB_PORT"
}

pids=()
REDIS_CONTAINER_ID=""
cleanup() {
  local code=$?
  for pid in "${pids[@]:-}"; do
    kill "$pid" >/dev/null 2>&1 || true
  done
  wait "${pids[@]:-}" >/dev/null 2>&1 || true
  if [[ -n "$REDIS_CONTAINER_ID" ]]; then
    docker rm -fv "$REDIS_CONTAINER_ID" >/dev/null 2>&1 || true
  fi
  if [[ "$code" -ne 0 ]]; then
    echo "E2E failed. log_dir=$LOG_DIR" >&2
    for file in "$LOG_DIR"/*.stderr.log; do
      if [[ -f "$file" ]]; then
        echo "----- $file -----" >&2
        tail -n 80 "$file" >&2 || true
      fi
    done
  fi
}
trap cleanup EXIT

start_server() {
  local name="$1"
  local main="$2"
  shift
  shift
  node "$main" "$@" >"$LOG_DIR/$name.stdout.log" 2>"$LOG_DIR/$name.stderr.log" &
  pids+=("$!")
}

start_configured_server() {
  local name="$1"
  local main="$2"
  shift 2
  local config="$LOG_DIR/$name.config.json"
  node "$ROOT_DIR/write-config.mjs" "$config" "$@"
  start_server "$name" "$main" --config "$config"
}

start_role() {
  case "$1" in
    actor)
      start_configured_server actor "$ACTOR_MAIN" \
        --rid to-actor-owner \
        --http-url "$ACTOR_URL" \
        --redis-endpoint "$REDIS_ENDPOINT" \
        --redis-key-prefix "$REDIS_KEY_PREFIX" \
        --router-endpoint "tcp://127.0.0.1:$ACTOR_ROUTER_PORT" \
        --pubsub-endpoint "tcp://127.0.0.1:$ACTOR_PUBSUB_PORT" \
        --evidence-file "$LOG_DIR/actor.evidence.log" \
        --log-dir "$LOG_DIR"
      ;;
    caller)
      start_configured_server caller "$CALLER_MAIN" \
        --rid to-actor-caller \
        --http-url "$CALLER_URL" \
        --redis-endpoint "$REDIS_ENDPOINT" \
        --redis-key-prefix "$REDIS_KEY_PREFIX" \
        --router-endpoint "tcp://127.0.0.1:$CALLER_ROUTER_PORT" \
        --pubsub-endpoint "tcp://127.0.0.1:$CALLER_PUBSUB_PORT" \
        --log-dir "$LOG_DIR"
      ;;
    session)
      start_configured_server session "$SESSION_MAIN" \
        --rid to-actor-session \
        --http-url "$SESSION_URL" \
        --stream-endpoint "$SESSION_STREAM_ENDPOINT" \
        --redis-endpoint "$REDIS_ENDPOINT" \
        --redis-key-prefix "$REDIS_KEY_PREFIX" \
        --router-endpoint "tcp://127.0.0.1:$SESSION_ROUTER_PORT" \
        --pubsub-endpoint "tcp://127.0.0.1:$SESSION_PUBSUB_PORT" \
        --log-dir "$LOG_DIR"
      ;;
    *) echo "Unknown server role '$1'" >&2; return 1 ;;
  esac
}

wait_role() {
  case "$1" in
    actor) wait_health "$ACTOR_URL" actor ;;
    caller) wait_health "$CALLER_URL" caller ;;
    session) wait_health "$SESSION_URL" session && wait_tcp session-stream "$SESSION_STREAM_ENDPOINT" ;;
    *) echo "Unknown server role '$1'" >&2; return 1 ;;
  esac
}

echo "log_dir=$LOG_DIR"
echo "start_order=$E2E_START_ORDER"

(cd "$NODE_ROOT" && npm run build >/dev/null)
(cd "$ROOT_DIR/Server/Actor" && npm run build >/dev/null)
(cd "$ROOT_DIR/Server/Caller" && npm run build >/dev/null)
(cd "$ROOT_DIR/Server/Session" && npm run build >/dev/null)
(cd "$ROOT_DIR/Client" && npm run build >/dev/null)

if ! command -v docker >/dev/null 2>&1; then
  echo "Docker is required." >&2
  exit 1
fi
start_redis_container "zlink-redis-node-e2e-${RANDOM}-$$" -p "127.0.0.1::6379" "redis:7.2-alpine"
REDIS_ENDPOINT="$(redis_container_endpoint "$REDIS_CONTAINER_ID")"
wait_tcp redis "tcp://$REDIS_ENDPOINT"
REDIS_KEY_PREFIX="to-actor-messaging:node:$RUN_ID"

ACTOR_HTTP_PORT="$(pick_port)"
SESSION_HTTP_PORT="$(pick_port)"
CALLER_HTTP_PORT="$(pick_port)"
ACTOR_ROUTER_PORT="$(pick_port)"
ACTOR_PUBSUB_PORT="$(pick_port)"
SESSION_ROUTER_PORT="$(pick_port)"
SESSION_PUBSUB_PORT="$(pick_port)"
SESSION_STREAM_PORT="$(pick_port)"
CALLER_ROUTER_PORT="$(pick_port)"
CALLER_PUBSUB_PORT="$(pick_port)"

ACTOR_URL="http://127.0.0.1:$ACTOR_HTTP_PORT"
SESSION_URL="http://127.0.0.1:$SESSION_HTTP_PORT"
SESSION_STREAM_ENDPOINT="ws://127.0.0.1:$SESSION_STREAM_PORT"
CALLER_URL="http://127.0.0.1:$CALLER_HTTP_PORT"
ACTOR_MAIN="$ROOT_DIR/Server/Actor/dist/Server/Actor/main.js"
SESSION_MAIN="$ROOT_DIR/Server/Session/dist/Server/Session/main.js"
CALLER_MAIN="$ROOT_DIR/Server/Caller/dist/Server/Caller/main.js"

SERVER_ROLES=(actor session caller)
mapfile -t ORDERED_SERVER_ROLES < <(ordered_e2e_roles "$E2E_START_ORDER" "${SERVER_ROLES[@]}")
for role in "${ORDERED_SERVER_ROLES[@]}"; do
  start_role "$role"
done
for role in "${SERVER_ROLES[@]}"; do
  wait_role "$role"
done
wait_topology

node "$NODE_ROOT/scripts/browser-e2e/run-e2e-client.mjs" "$ROOT_DIR/Client/main.ts" -- \
  --actor-url "$ACTOR_URL" \
  --caller-url "$CALLER_URL" \
  --session-url "$SESSION_URL" \
  --session-stream-endpoint "$SESSION_STREAM_ENDPOINT" \
  --scenario "$SCENARIO" \
  >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"

cat "$LOG_DIR/client.stdout.log"
