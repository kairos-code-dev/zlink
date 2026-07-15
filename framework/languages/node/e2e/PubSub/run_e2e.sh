#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NODE_ROOT="$(cd "$ROOT_DIR/../.." && pwd)"
source "$NODE_ROOT/e2e/redis-container.sh"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
SCENARIO="${1:-all}"
LOCAL_READINESS_TIMEOUT_SECONDS=3
LOCAL_READINESS_POLL_SECONDS=0.1
LOCAL_READINESS_ATTEMPTS=30
ROUTE_SETTLE_TIMEOUT_SECONDS=5
SCENARIO_SETTLE_TIMEOUT_SECONDS=3
HTTP_PROBE_TIMEOUT_SECONDS=3
mkdir -p "$LOG_DIR"

pick_port() {
  node "$NODE_ROOT/e2e/port-picker.js"
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
    docker rm -fv "$REDIS_CONTAINER_ID" >/dev/null 2>&1 || true
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

start_configured_server() {
  local name="$1"; local main="$2"; shift 2
  local config="$LOG_DIR/$name.config.json"
  node "$ROOT_DIR/write-config.mjs" "$config" "$@"
  start_server "$name" "$main" --config "$config"
}

echo "log_dir=$LOG_DIR"

(cd "$NODE_ROOT" && npm run build >/dev/null)
build_package "$ROOT_DIR/Server/Publisher"
build_package "$ROOT_DIR/Server/Subscriber"
build_package "$ROOT_DIR/Client"

if ! command -v docker >/dev/null 2>&1; then
  echo "Docker is required to run PubSub because it provisions a dedicated Redis location store." >&2
  exit 1
fi

start_redis_container "zlink-redis-node-pubsub-${RANDOM}-$$" -p "127.0.0.1::6379" "redis:7.2-alpine"
REDIS_ENDPOINT="$(redis_container_endpoint "$REDIS_CONTAINER_ID")"
REDIS_KEY_PREFIX="pubsub:node:$RUN_ID"

PUB_HTTP_PORT="$(pick_port)"
SUB_1_HTTP_PORT="$(pick_port)"
SUB_2_HTTP_PORT="$(pick_port)"
SUB_3_HTTP_PORT="$(pick_port)"
SUB_LATE_HTTP_PORT="$(pick_port)"
PUB_PORT="$(pick_port)"

PUB_ENDPOINT="tcp://127.0.0.1:$PUB_PORT"
PUB_URL="http://127.0.0.1:$PUB_HTTP_PORT"
SUB_1_URL="http://127.0.0.1:$SUB_1_HTTP_PORT"
SUB_2_URL="http://127.0.0.1:$SUB_2_HTTP_PORT"
SUB_3_URL="http://127.0.0.1:$SUB_3_HTTP_PORT"
SUB_LATE_URL="http://127.0.0.1:$SUB_LATE_HTTP_PORT"

PUBLISHER_MAIN="$ROOT_DIR/Server/Publisher/dist/Server/Publisher/main.js"
SUBSCRIBER_MAIN="$ROOT_DIR/Server/Subscriber/dist/Server/Subscriber/main.js"
CLIENT_MAIN="$ROOT_DIR/Client/dist/Client/main.js"

start_configured_server pub-a "$PUBLISHER_MAIN" \
  --rid pub-a \
  --http-url "$PUB_URL" \
  --publisher-endpoint "$PUB_ENDPOINT" \
  --redis-endpoint "$REDIS_ENDPOINT" \
  --redis-key-prefix "$REDIS_KEY_PREFIX" \
  --evidence-file "$LOG_DIR/pub-a.evidence.log" \
  --log-dir "$LOG_DIR"
wait_health "$PUB_URL" pub-a

for sub in 1 2 3; do
  url_var="SUB_${sub}_URL"
  extra_args=()
  if [[ "$sub" == "3" ]]; then
    extra_args+=(--handler-delay-ms 3000)
  fi
  start_configured_server "sub-$sub" "$SUBSCRIBER_MAIN" \
    --rid "sub-$sub" \
    --http-url "${!url_var}" \
    --redis-endpoint "$REDIS_ENDPOINT" \
    --redis-key-prefix "$REDIS_KEY_PREFIX" \
    --evidence-file "$LOG_DIR/sub-$sub.evidence.log" \
    --log-dir "$LOG_DIR" \
    "${extra_args[@]}"
  wait_health "${!url_var}" "sub-$sub"
done

node "$CLIENT_MAIN" \
  --publisher-url "$PUB_URL" \
  --subscriber-url "$SUB_1_URL" \
  --subscriber-url "$SUB_2_URL" \
  --subscriber-url "$SUB_3_URL" \
  --late-subscriber-url "$SUB_LATE_URL" \
  --publisher-endpoint "$PUB_ENDPOINT" \
  --redis-endpoint "$REDIS_ENDPOINT" \
  --redis-key-prefix "$REDIS_KEY_PREFIX" \
  --publisher-main "$PUBLISHER_MAIN" \
  --subscriber-main "$SUBSCRIBER_MAIN" \
  --log-dir "$LOG_DIR" \
  --scenario "$SCENARIO" \
  >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"

cat "$LOG_DIR/client.stdout.log"
