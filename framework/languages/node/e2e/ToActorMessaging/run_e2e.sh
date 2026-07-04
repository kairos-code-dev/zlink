#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NODE_ROOT="$(cd "$ROOT_DIR/../.." && pwd)"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"

pick_port() {
  node -e "const net=require('node:net'); const s=net.createServer(); s.listen(0,'127.0.0.1',()=>{console.log(s.address().port); s.close();});"
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
  local host_port="${endpoint#tcp://}"
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

pids=()
REDIS_CONTAINER_ID=""
cleanup() {
  local code=$?
  for pid in "${pids[@]:-}"; do
    kill "$pid" >/dev/null 2>&1 || true
  done
  wait "${pids[@]:-}" >/dev/null 2>&1 || true
  if [[ -n "$REDIS_CONTAINER_ID" ]]; then
    docker rm -f "$REDIS_CONTAINER_ID" >/dev/null 2>&1 || true
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

echo "log_dir=$LOG_DIR"

(cd "$NODE_ROOT" && npm run build >/dev/null)
(cd "$ROOT_DIR/Server/Actor" && npm run build >/dev/null)
(cd "$ROOT_DIR/Server/Caller" && npm run build >/dev/null)
(cd "$ROOT_DIR/Client" && npm run build >/dev/null)

if [[ -n "${ZLINK_REDIS_E2E_ENDPOINT:-}" ]]; then
  REDIS_ENDPOINT="$ZLINK_REDIS_E2E_ENDPOINT"
else
  if ! command -v docker >/dev/null 2>&1; then
    echo "Docker is required unless ZLINK_REDIS_E2E_ENDPOINT is set." >&2
    exit 1
  fi
  REDIS_CONTAINER_ID="$(docker run -d --rm --name "to-actor-messaging-node-redis-${RANDOM}-$$" -p "127.0.0.1::6379" redis:7.2-alpine)"
  REDIS_PORT="$(docker port "$REDIS_CONTAINER_ID" 6379/tcp | sed 's/.*://')"
  REDIS_ENDPOINT="127.0.0.1:$REDIS_PORT"
fi
wait_tcp redis "tcp://$REDIS_ENDPOINT"
REDIS_KEY_PREFIX="to-actor-messaging:node:$RUN_ID"

ACTOR_HTTP_PORT="$(pick_port)"
CALLER_HTTP_PORT="$(pick_port)"
ACTOR_ROUTER_PORT="$(pick_port)"
ACTOR_PUBSUB_PORT="$(pick_port)"
CALLER_ROUTER_PORT="$(pick_port)"
CALLER_PUBSUB_PORT="$(pick_port)"

ACTOR_URL="http://127.0.0.1:$ACTOR_HTTP_PORT"
CALLER_URL="http://127.0.0.1:$CALLER_HTTP_PORT"
ACTOR_MAIN="$ROOT_DIR/Server/Actor/dist/Server/Actor/main.js"
CALLER_MAIN="$ROOT_DIR/Server/Caller/dist/Server/Caller/main.js"
CLIENT_MAIN="$ROOT_DIR/Client/dist/Client/main.js"

start_server actor "$ACTOR_MAIN" \
  --rid to-actor-owner \
  --http-url "$ACTOR_URL" \
  --redis-endpoint "$REDIS_ENDPOINT" \
  --redis-key-prefix "$REDIS_KEY_PREFIX" \
  --router-endpoint "tcp://127.0.0.1:$ACTOR_ROUTER_PORT" \
  --pubsub-endpoint "tcp://127.0.0.1:$ACTOR_PUBSUB_PORT" \
  --evidence-file "$LOG_DIR/actor.evidence.log" \
  --log-dir "$LOG_DIR"

start_server caller "$CALLER_MAIN" \
  --rid to-actor-caller \
  --http-url "$CALLER_URL" \
  --redis-endpoint "$REDIS_ENDPOINT" \
  --redis-key-prefix "$REDIS_KEY_PREFIX" \
  --router-endpoint "tcp://127.0.0.1:$CALLER_ROUTER_PORT" \
  --pubsub-endpoint "tcp://127.0.0.1:$CALLER_PUBSUB_PORT" \
  --log-dir "$LOG_DIR"

wait_health "$ACTOR_URL" actor
wait_health "$CALLER_URL" caller
sleep 5

node "$CLIENT_MAIN" \
  --actor-url "$ACTOR_URL" \
  --caller-url "$CALLER_URL" \
  >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"

cat "$LOG_DIR/client.stdout.log"
