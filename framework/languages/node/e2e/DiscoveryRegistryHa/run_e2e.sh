#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NODE_ROOT="$(cd "$ROOT_DIR/../.." && pwd)"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
SCENARIO="${1:-all}"
LOCAL_READINESS_POLL_SECONDS=0.1
LOCAL_READINESS_ATTEMPTS=30
HTTP_PROBE_TIMEOUT_SECONDS=3
SCENARIOS=(
  SF-A1
  SF-B1
  SF-C1
  SF-D1
  SF-D2
)
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
LAST_STARTED_PID=""
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
  ZLINK_E2E_RID="$name" node "$main" "$@" >"$LOG_DIR/$name.stdout.log" 2>"$LOG_DIR/$name.stderr.log" &
  LAST_STARTED_PID="$!"
  pids+=("$LAST_STARTED_PID")
}

kill_pid() {
  local pid="$1"
  if kill -0 "$pid" 2>/dev/null; then
    kill -9 "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  fi
}

run_all_scenarios() {
  local scenario
  for scenario in "${SCENARIOS[@]}"; do
    "$0" "$scenario"
  done
  echo "store-failure-recovery e2e result=passed"
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

if [[ "$SCENARIO" == "all" ]]; then
  run_all_scenarios
  exit 0
fi

(cd "$NODE_ROOT" && npm run build >/dev/null)
build_package "$ROOT_DIR/Server/LocationProbe"
build_package "$ROOT_DIR/Server/Provider"
build_package "$ROOT_DIR/Server/Consumer"
build_package "$ROOT_DIR/Client"

if ! command -v docker >/dev/null 2>&1; then
  echo "Docker is required to run DiscoveryRegistryHa because it provisions a dedicated Redis location store." >&2
  exit 1
fi

REDIS_CONTAINER_ID="$(docker run -d --rm --name "store-failure-node-redis-${RANDOM}-$$" -p "127.0.0.1::6379" redis:7.2-alpine)"
REDIS_PORT="$(docker port "$REDIS_CONTAINER_ID" 6379/tcp | sed 's/.*://')"
REDIS_ENDPOINT="127.0.0.1:$REDIS_PORT"
REDIS_KEY_PREFIX="store-failure:node:$RUN_ID"
wait_tcp redis "tcp://$REDIS_ENDPOINT"

LOCATION_PROBE_MAIN="$ROOT_DIR/Server/LocationProbe/dist/Server/LocationProbe/main.js"
PROVIDER_MAIN="$ROOT_DIR/Server/Provider/dist/Server/Provider/main.js"
CONSUMER_MAIN="$ROOT_DIR/Server/Consumer/dist/Server/Consumer/main.js"
CLIENT_MAIN="$ROOT_DIR/Client/dist/Client/main.js"

start_topology() {
  local reg_http_port consumer_http_port provider_a_http_port provider_b_http_port
  local provider_a_channel_port provider_b_channel_port
  reg_http_port="$(pick_port)"
  consumer_http_port="$(pick_port)"
  provider_a_http_port="$(pick_port)"
  provider_b_http_port="$(pick_port)"
  provider_a_channel_port="$(pick_port)"
  provider_b_channel_port="$(pick_port)"

  LOCATION_PROBE_URL="http://127.0.0.1:$reg_http_port"
  CONSUMER_URL="http://127.0.0.1:$consumer_http_port"
  PROVIDER_A_URL="http://127.0.0.1:$provider_a_http_port"
  PROVIDER_B_URL="http://127.0.0.1:$provider_b_http_port"

  start_server reg-1 "$LOCATION_PROBE_MAIN" \
    --rid reg-1 \
    --probe-id 1 \
    --http-url "$LOCATION_PROBE_URL" \
    --redis-endpoint "$REDIS_ENDPOINT" \
    --redis-key-prefix "$REDIS_KEY_PREFIX" \
    --log-dir "$LOG_DIR"
  wait_health "$LOCATION_PROBE_URL" reg-1

  start_server api-a "$PROVIDER_MAIN" \
    --rid api-a \
    --http-url "$PROVIDER_A_URL" \
    --redis-endpoint "$REDIS_ENDPOINT" \
    --redis-key-prefix "$REDIS_KEY_PREFIX" \
    --channel-endpoint "tcp://127.0.0.1:$provider_a_channel_port" \
    --evidence-file "$LOG_DIR/api-a.evidence.log" \
    --log-dir "$LOG_DIR"
  wait_health "$PROVIDER_A_URL" api-a

  start_server api-b "$PROVIDER_MAIN" \
    --rid api-b \
    --http-url "$PROVIDER_B_URL" \
    --redis-endpoint "$REDIS_ENDPOINT" \
    --redis-key-prefix "$REDIS_KEY_PREFIX" \
    --channel-endpoint "tcp://127.0.0.1:$provider_b_channel_port" \
    --evidence-file "$LOG_DIR/api-b.evidence.log" \
    --log-dir "$LOG_DIR"
  API_B_PID="$LAST_STARTED_PID"
  wait_health "$PROVIDER_B_URL" api-b

  start_server consumer "$CONSUMER_MAIN" \
    --http-url "$CONSUMER_URL" \
    --redis-endpoint "$REDIS_ENDPOINT" \
    --redis-key-prefix "$REDIS_KEY_PREFIX" \
    --trace-label consumer \
    --log-dir "$LOG_DIR"
  wait_health "$CONSUMER_URL" consumer
}

run_client() {
  local scenario="$1"
  local stdout="$2"
  local stderr="$3"
  node "$CLIENT_MAIN" \
    --topology-url "$LOCATION_PROBE_URL" \
    --consumer-url "$CONSUMER_URL" \
    --provider-a-url "$PROVIDER_A_URL" \
    --provider-b-url "$PROVIDER_B_URL" \
    --scenario "$scenario" \
    >"$stdout" 2>"$stderr"
}

run_warmup() {
  run_client SF-A1 "$LOG_DIR/client-warmup.stdout.log" "$LOG_DIR/client-warmup.stderr.log"
}

run_sf_a1() {
  start_topology
  run_client SF-A1 "$LOG_DIR/client.stdout.log" "$LOG_DIR/client.stderr.log"
}

run_sf_b1() {
  start_topology
  run_warmup
  docker stop "$REDIS_CONTAINER_ID" >/dev/null
  REDIS_CONTAINER_ID=""
  run_client SF-B1 "$LOG_DIR/client.stdout.log" "$LOG_DIR/client.stderr.log"
}

run_sf_c1() {
  start_topology
  run_warmup
  kill_pid "$API_B_PID"
  run_client SF-C1 "$LOG_DIR/client.stdout.log" "$LOG_DIR/client.stderr.log"
}

run_sf_d1() {
  local client_pid
  start_topology
  run_warmup
  run_client SF-D1 "$LOG_DIR/client.stdout.log" "$LOG_DIR/client.stderr.log" &
  client_pid="$!"
  sleep 0.5
  docker pause "$REDIS_CONTAINER_ID" >/dev/null
  sleep 1.5
  docker unpause "$REDIS_CONTAINER_ID" >/dev/null
  wait "$client_pid"
}

run_sf_d2() {
  local client_pid
  start_topology
  run_warmup
  run_client SF-D2 "$LOG_DIR/client.stdout.log" "$LOG_DIR/client.stderr.log" &
  client_pid="$!"
  sleep 0.5
  docker pause "$REDIS_CONTAINER_ID" >/dev/null
  kill_pid "$API_B_PID"
  sleep 4
  docker unpause "$REDIS_CONTAINER_ID" >/dev/null
  wait "$client_pid"
}

case "$SCENARIO" in
  SF-A1)
    run_sf_a1
    cat "$LOG_DIR/client.stdout.log"
    ;;
  SF-B1)
    run_sf_b1
    cat "$LOG_DIR/client-warmup.stdout.log"
    cat "$LOG_DIR/client.stdout.log"
    ;;
  SF-C1)
    run_sf_c1
    cat "$LOG_DIR/client-warmup.stdout.log"
    cat "$LOG_DIR/client.stdout.log"
    ;;
  SF-D1)
    run_sf_d1
    cat "$LOG_DIR/client-warmup.stdout.log"
    cat "$LOG_DIR/client.stdout.log"
    ;;
  SF-D2)
    run_sf_d2
    cat "$LOG_DIR/client-warmup.stdout.log"
    cat "$LOG_DIR/client.stdout.log"
    ;;
  *)
    echo "Unsupported scenario '$SCENARIO'. Supported: all, ${SCENARIOS[*]}" >&2
    exit 2
    ;;
esac
