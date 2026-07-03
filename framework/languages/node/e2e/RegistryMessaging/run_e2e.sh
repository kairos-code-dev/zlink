#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NODE_ROOT="$(cd "$ROOT_DIR/../.." && pwd)"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
SCENARIO="${1:-all}"
LOCAL_READINESS_TIMEOUT_SECONDS=3
LOCAL_READINESS_POLL_SECONDS=0.1
LOCAL_READINESS_ATTEMPTS=30
HTTP_PROBE_TIMEOUT_SECONDS=3
mkdir -p "$LOG_DIR"

pick_port() {
  node -e "const net=require('node:net'); const s=net.createServer(); s.listen(0,'127.0.0.1',()=>{console.log(s.address().port); s.close();});"
}

build_package() {
  local dir="$1"
  (cd "$dir" && npm run build >/dev/null)
}

wait_health() {
  local url="$1"
  local name="$2"
  for _ in $(seq 1 "${LOCAL_READINESS_ATTEMPTS}"); do
    if curl --max-time "${HTTP_PROBE_TIMEOUT_SECONDS}" -fsS "$url/health" >/dev/null 2>&1; then
      return 0
    fi
    sleep "${LOCAL_READINESS_POLL_SECONDS}"
  done
  echo "Timed out waiting for $name at $url" >&2
  return 1
}

pids=()
REDIS_CONTAINER_ID=""
cleanup() {
  local code=$?
  for pid in "${pids[@]:-}"; do
    if kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
    fi
  done
  wait "${pids[@]:-}" 2>/dev/null || true
  if [[ -n "$REDIS_CONTAINER_ID" ]]; then
    docker rm -f "$REDIS_CONTAINER_ID" >/dev/null 2>&1 || true
  fi
  if [[ "$code" -ne 0 ]]; then
    echo "E2E failed. log_dir=$LOG_DIR" >&2
    for file in "$LOG_DIR"/*.stderr.log "$LOG_DIR"/client.stderr.log; do
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

echo "log_dir=$LOG_DIR"

(cd "$NODE_ROOT" && npm run build >/dev/null)
build_package "$ROOT_DIR/Server/LocationProbe"
build_package "$ROOT_DIR/Server/Provider"
build_package "$ROOT_DIR/Server/Workflow"
build_package "$ROOT_DIR/Server/Consumer"
build_package "$ROOT_DIR/Client"

if ! command -v docker >/dev/null 2>&1; then
  echo "Docker is required to run RegistryMessaging because it provisions a dedicated Redis location store." >&2
  exit 1
fi

REDIS_CONTAINER_ID="$(docker run -d --rm --name "location-messaging-node-redis-${RANDOM}-$$" -p "127.0.0.1::6379" redis:7.2-alpine)"
REDIS_PORT="$(docker port "$REDIS_CONTAINER_ID" 6379/tcp | sed 's/.*://')"
REDIS_ENDPOINT="127.0.0.1:$REDIS_PORT"
REDIS_KEY_PREFIX="location-messaging:node:$RUN_ID"
wait_tcp redis "tcp://$REDIS_ENDPOINT"

LOCATION_PROBE_HTTP_PORT="$(pick_port)"
PROVIDER_A_HTTP_PORT="$(pick_port)"
PROVIDER_B_HTTP_PORT="$(pick_port)"
WORKFLOW_HTTP_PORT="$(pick_port)"
CONSUMER_HTTP_PORT="$(pick_port)"
SINGLE_CONSUMER_HTTP_PORT="$(pick_port)"
BACKPRESSURE_CONSUMER_HTTP_PORT="$(pick_port)"
LOCATION_CONSUMER_HTTP_PORT="$(pick_port)"
API_A_PORT="$(pick_port)"
API_B_PORT="$(pick_port)"
WORKFLOW_PORT="$(pick_port)"
ROUTE_A_PORT="$(pick_port)"
ROUTE_B_PORT="$(pick_port)"

API_A="tcp://127.0.0.1:$API_A_PORT"
API_B="tcp://127.0.0.1:$API_B_PORT"
WORKFLOW="tcp://127.0.0.1:$WORKFLOW_PORT"
ROUTE_A="tcp://127.0.0.1:$ROUTE_A_PORT"
ROUTE_B="tcp://127.0.0.1:$ROUTE_B_PORT"

LOCATION_PROBE_MAIN="$ROOT_DIR/Server/LocationProbe/dist/Server/LocationProbe/main.js"
PROVIDER_MAIN="$ROOT_DIR/Server/Provider/dist/Server/Provider/main.js"
WORKFLOW_MAIN="$ROOT_DIR/Server/Workflow/dist/Server/Workflow/main.js"
CONSUMER_MAIN="$ROOT_DIR/Server/Consumer/dist/Server/Consumer/main.js"
CLIENT_MAIN="$ROOT_DIR/Client/dist/Client/main.js"

start_server location-probe "$LOCATION_PROBE_MAIN" \
  --rid location-probe \
  --http-url "http://127.0.0.1:$LOCATION_PROBE_HTTP_PORT" \
  --redis-endpoint "$REDIS_ENDPOINT" \
  --redis-key-prefix "$REDIS_KEY_PREFIX" \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$LOCATION_PROBE_HTTP_PORT" location-probe

start_server api-a "$PROVIDER_MAIN" \
  --rid api-a \
  --http-url "http://127.0.0.1:$PROVIDER_A_HTTP_PORT" \
  --redis-endpoint "$REDIS_ENDPOINT" \
  --redis-key-prefix "$REDIS_KEY_PREFIX" \
  --channel-endpoint "$API_A" \
  --manual-client-endpoint "$API_A" \
  --route-endpoint "$ROUTE_A" \
  --route-peer "$ROUTE_B" \
  --evidence-file "$LOG_DIR/api-a.evidence.log" \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$PROVIDER_A_HTTP_PORT" api-a

start_server api-b "$PROVIDER_MAIN" \
  --rid api-b \
  --http-url "http://127.0.0.1:$PROVIDER_B_HTTP_PORT" \
  --redis-endpoint "$REDIS_ENDPOINT" \
  --redis-key-prefix "$REDIS_KEY_PREFIX" \
  --channel-endpoint "$API_B" \
  --manual-client-endpoint "$API_B" \
  --route-endpoint "$ROUTE_B" \
  --route-peer "$ROUTE_A" \
  --evidence-file "$LOG_DIR/api-b.evidence.log" \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$PROVIDER_B_HTTP_PORT" api-b

start_server workflow-a "$WORKFLOW_MAIN" \
  --rid workflow-a \
  --http-url "http://127.0.0.1:$WORKFLOW_HTTP_PORT" \
  --redis-endpoint "$REDIS_ENDPOINT" \
  --redis-key-prefix "$REDIS_KEY_PREFIX" \
  --workflow-endpoint "$WORKFLOW" \
  --evidence-file "$LOG_DIR/workflow-a.evidence.log" \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$WORKFLOW_HTTP_PORT" workflow-a

start_server direct-consumer "$CONSUMER_MAIN" \
  --http-url "http://127.0.0.1:$CONSUMER_HTTP_PORT" \
  --provider-endpoint "$API_A" \
  --provider-endpoint "$API_B" \
  --trace-label direct-consumer \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$CONSUMER_HTTP_PORT" direct-consumer

start_server single-consumer "$CONSUMER_MAIN" \
  --http-url "http://127.0.0.1:$SINGLE_CONSUMER_HTTP_PORT" \
  --provider-endpoint "$API_A" \
  --trace-label single-consumer \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$SINGLE_CONSUMER_HTTP_PORT" single-consumer

start_server backpressure-consumer "$CONSUMER_MAIN" \
  --http-url "http://127.0.0.1:$BACKPRESSURE_CONSUMER_HTTP_PORT" \
  --provider-endpoint "$API_A" \
  --trace-label backpressure-consumer \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$BACKPRESSURE_CONSUMER_HTTP_PORT" backpressure-consumer

start_server location-consumer "$CONSUMER_MAIN" \
  --http-url "http://127.0.0.1:$LOCATION_CONSUMER_HTTP_PORT" \
  --redis-endpoint "$REDIS_ENDPOINT" \
  --redis-key-prefix "$REDIS_KEY_PREFIX" \
  --trace-label location-consumer \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$LOCATION_CONSUMER_HTTP_PORT" location-consumer

node "$CLIENT_MAIN" \
  --topology-url "http://127.0.0.1:$LOCATION_PROBE_HTTP_PORT" \
  --provider-a-url "http://127.0.0.1:$PROVIDER_A_HTTP_PORT" \
  --provider-b-url "http://127.0.0.1:$PROVIDER_B_HTTP_PORT" \
  --workflow-url "http://127.0.0.1:$WORKFLOW_HTTP_PORT" \
  --direct-consumer-url "http://127.0.0.1:$CONSUMER_HTTP_PORT" \
  --single-consumer-url "http://127.0.0.1:$SINGLE_CONSUMER_HTTP_PORT" \
  --backpressure-consumer-url "http://127.0.0.1:$BACKPRESSURE_CONSUMER_HTTP_PORT" \
  --location-consumer-url "http://127.0.0.1:$LOCATION_CONSUMER_HTTP_PORT" \
  --location-probe-main "$LOCATION_PROBE_MAIN" \
  --provider-main "$PROVIDER_MAIN" \
  --redis-endpoint "$REDIS_ENDPOINT" \
  --redis-key-prefix "$REDIS_KEY_PREFIX" \
  --log-dir "$LOG_DIR" \
  --scenario "$SCENARIO" \
  >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"

cat "$LOG_DIR/client.stdout.log"
