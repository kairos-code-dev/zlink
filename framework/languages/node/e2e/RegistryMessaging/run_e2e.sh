#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NODE_ROOT="$(cd "$ROOT_DIR/../.." && pwd)"
source "$NODE_ROOT/e2e/redis-container.sh"
source "$NODE_ROOT/e2e/runner-common.sh"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
SCENARIO="${1:-all}"
LOCAL_READINESS_TIMEOUT_SECONDS=3
LOCAL_READINESS_POLL_SECONDS=0.1
LOCAL_READINESS_ATTEMPTS=30
HTTP_PROBE_TIMEOUT_SECONDS=3
mkdir -p "$LOG_DIR"

pids=()
REDIS_CONTAINER_ID=""
cleanup() {
  local code=$?
  stop_live_pids
  wait_all_pids_ignoring_status
  remove_redis_container
  if [[ "$code" -ne 0 ]]; then
    tail_failure_logs
  fi
}
trap cleanup EXIT

echo "log_dir=$LOG_DIR"

(cd "$NODE_ROOT" && npm run build >/dev/null)
build_package "$ROOT_DIR/Server/Provider"
build_package "$ROOT_DIR/Server/Workflow"
build_package "$ROOT_DIR/Server/Consumer"
build_package "$ROOT_DIR/Client"

if ! command -v docker >/dev/null 2>&1; then
  echo "Docker is required to run RegistryMessaging because it provisions a dedicated Redis location store." >&2
  exit 1
fi

start_redis_container "zlink-redis-node-e2e-${RANDOM}-$$" -p "127.0.0.1::6379" "redis:7.2-alpine"
REDIS_ENDPOINT="$(redis_container_endpoint "$REDIS_CONTAINER_ID")"
REDIS_KEY_PREFIX="location-messaging:node:$RUN_ID"
wait_tcp redis "tcp://$REDIS_ENDPOINT"

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

PROVIDER_MAIN="$ROOT_DIR/Server/Provider/dist/Server/Provider/main.js"
WORKFLOW_MAIN="$ROOT_DIR/Server/Workflow/dist/Server/Workflow/main.js"
CONSUMER_MAIN="$ROOT_DIR/Server/Consumer/dist/Server/Consumer/main.js"
CLIENT_MAIN="$ROOT_DIR/Client/dist/Client/main.js"

start_configured_server() {
  local name="$1"; local main="$2"; shift 2
  local config="$LOG_DIR/$name.config.json"
  node "$ROOT_DIR/write-config.mjs" "$config" "$@"
  start_server "$name" "$main" --config "$config"
}

start_configured_server api-a "$PROVIDER_MAIN" \
  --rid api-a \
  --http-url "http://127.0.0.1:$PROVIDER_A_HTTP_PORT" \
  --redis-endpoint "$REDIS_ENDPOINT" \
  --redis-key-prefix "$REDIS_KEY_PREFIX" \
  --channel-endpoint "$API_A" \
  --manual-client-endpoint "$API_A" \
  --route-endpoint "$ROUTE_A" \
  --route-peer "$ROUTE_B" \
  --max-message-size "$((2 * 1024 * 1024))" \
  --evidence-file "$LOG_DIR/api-a.evidence.log" \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$PROVIDER_A_HTTP_PORT" api-a

start_configured_server api-b "$PROVIDER_MAIN" \
  --rid api-b \
  --http-url "http://127.0.0.1:$PROVIDER_B_HTTP_PORT" \
  --redis-endpoint "$REDIS_ENDPOINT" \
  --redis-key-prefix "$REDIS_KEY_PREFIX" \
  --channel-endpoint "$API_B" \
  --manual-client-endpoint "$API_B" \
  --route-endpoint "$ROUTE_B" \
  --route-peer "$ROUTE_A" \
  --max-message-size "$((2 * 1024 * 1024))" \
  --evidence-file "$LOG_DIR/api-b.evidence.log" \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$PROVIDER_B_HTTP_PORT" api-b

start_configured_server workflow-a "$WORKFLOW_MAIN" \
  --rid workflow-a \
  --http-url "http://127.0.0.1:$WORKFLOW_HTTP_PORT" \
  --redis-endpoint "$REDIS_ENDPOINT" \
  --redis-key-prefix "$REDIS_KEY_PREFIX" \
  --workflow-endpoint "$WORKFLOW" \
  --evidence-file "$LOG_DIR/workflow-a.evidence.log" \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$WORKFLOW_HTTP_PORT" workflow-a

start_configured_server direct-consumer "$CONSUMER_MAIN" \
  --http-url "http://127.0.0.1:$CONSUMER_HTTP_PORT" \
  --provider-endpoint "$API_A" \
  --provider-endpoint "$API_B" \
  --trace-label direct-consumer \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$CONSUMER_HTTP_PORT" direct-consumer

start_configured_server single-consumer "$CONSUMER_MAIN" \
  --http-url "http://127.0.0.1:$SINGLE_CONSUMER_HTTP_PORT" \
  --provider-endpoint "$API_A" \
  --trace-label single-consumer \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$SINGLE_CONSUMER_HTTP_PORT" single-consumer

start_configured_server backpressure-consumer "$CONSUMER_MAIN" \
  --http-url "http://127.0.0.1:$BACKPRESSURE_CONSUMER_HTTP_PORT" \
  --provider-endpoint "$API_A" \
  --trace-label backpressure-consumer \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$BACKPRESSURE_CONSUMER_HTTP_PORT" backpressure-consumer

start_configured_server location-consumer "$CONSUMER_MAIN" \
  --http-url "http://127.0.0.1:$LOCATION_CONSUMER_HTTP_PORT" \
  --redis-endpoint "$REDIS_ENDPOINT" \
  --redis-key-prefix "$REDIS_KEY_PREFIX" \
  --trace-label location-consumer \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$LOCATION_CONSUMER_HTTP_PORT" location-consumer

node "$CLIENT_MAIN" \
  --provider-a-url "http://127.0.0.1:$PROVIDER_A_HTTP_PORT" \
  --provider-b-url "http://127.0.0.1:$PROVIDER_B_HTTP_PORT" \
  --workflow-url "http://127.0.0.1:$WORKFLOW_HTTP_PORT" \
  --direct-consumer-url "http://127.0.0.1:$CONSUMER_HTTP_PORT" \
  --single-consumer-url "http://127.0.0.1:$SINGLE_CONSUMER_HTTP_PORT" \
  --backpressure-consumer-url "http://127.0.0.1:$BACKPRESSURE_CONSUMER_HTTP_PORT" \
  --location-consumer-url "http://127.0.0.1:$LOCATION_CONSUMER_HTTP_PORT" \
  --provider-main "$PROVIDER_MAIN" \
  --consumer-main "$CONSUMER_MAIN" \
  --redis-endpoint "$REDIS_ENDPOINT" \
  --redis-key-prefix "$REDIS_KEY_PREFIX" \
  --log-dir "$LOG_DIR" \
  --scenario "$SCENARIO" \
  >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"

cat "$LOG_DIR/client.stdout.log"
