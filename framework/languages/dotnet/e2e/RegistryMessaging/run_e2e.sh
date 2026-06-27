#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"
SCENARIO="${1:-all}"

REGISTRY_PROJECT="$ROOT_DIR/Server/Registry/RegistryMessaging.Registry.csproj"
PROVIDER_PROJECT="$ROOT_DIR/Server/Provider/RegistryMessaging.Provider.csproj"
CONSUMER_PROJECT="$ROOT_DIR/Server/Consumer/RegistryMessaging.Consumer.csproj"
CLIENT_PROJECT="$ROOT_DIR/Client/RegistryMessaging.Client.csproj"

pick_port() {
  python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
}

REG_HTTP_PORT="$(pick_port)"
PROVIDER_A_HTTP_PORT="$(pick_port)"
PROVIDER_B_HTTP_PORT="$(pick_port)"
WORKFLOW_HTTP_PORT="$(pick_port)"
CONSUMER_HTTP_PORT="$(pick_port)"
SINGLE_CONSUMER_HTTP_PORT="$(pick_port)"
DISCOVERY_CONSUMER_HTTP_PORT="$(pick_port)"
BACKPRESSURE_CONSUMER_HTTP_PORT="$(pick_port)"
REG_PUB_PORT="$(pick_port)"
REG_ROUTER_PORT="$(pick_port)"
API_A_PORT="$(pick_port)"
API_B_PORT="$(pick_port)"
WORKFLOW_PORT="$(pick_port)"
ROUTE_A_PORT="$(pick_port)"
ROUTE_B_PORT="$(pick_port)"
CLIENT_ROUTE_PORT="$(pick_port)"

REG_PUB="tcp://127.0.0.1:$REG_PUB_PORT"
REG_ROUTER="tcp://127.0.0.1:$REG_ROUTER_PORT"
API_A="tcp://127.0.0.1:$API_A_PORT"
API_B="tcp://127.0.0.1:$API_B_PORT"
WORKFLOW="tcp://127.0.0.1:$WORKFLOW_PORT"
ROUTE_A="tcp://127.0.0.1:$ROUTE_A_PORT"
ROUTE_B="tcp://127.0.0.1:$ROUTE_B_PORT"
CLIENT_ROUTE="tcp://127.0.0.1:$CLIENT_ROUTE_PORT"

pids=()
cleanup() {
  local code=$?
  for pid in "${pids[@]:-}"; do
    if kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
    fi
  done
  wait "${pids[@]:-}" 2>/dev/null || true
  if [[ "$code" -ne 0 ]]; then
    echo "E2E failed. Logs: $LOG_DIR" >&2
  fi
}
trap cleanup EXIT

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

start_server() {
  local name="$1"
  local project="$2"
  shift
  shift
  dotnet run --project "$project" -- "$@" \
    >"$LOG_DIR/$name.stdout.log" 2>"$LOG_DIR/$name.stderr.log" &
  pids+=("$!")
}

echo "log_dir=$LOG_DIR"

start_server registry "$REGISTRY_PROJECT" \
  --rid registry \
  --http-url "http://127.0.0.1:$REG_HTTP_PORT" \
  --registry-pub-endpoint "$REG_PUB" \
  --registry-router-endpoint "$REG_ROUTER" \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$REG_HTTP_PORT" registry

start_server api-a "$PROVIDER_PROJECT" \
  --rid api-a \
  --http-url "http://127.0.0.1:$PROVIDER_A_HTTP_PORT" \
  --registry-router-endpoint "$REG_ROUTER" \
  --channel-endpoint "$API_A" \
  --manual-client-endpoint "$API_A" \
  --route-endpoint "$ROUTE_A" \
  --route-peer "$ROUTE_B" \
  --evidence-file "$LOG_DIR/api-a.evidence.log" \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$PROVIDER_A_HTTP_PORT" api-a

start_server api-b "$PROVIDER_PROJECT" \
  --rid api-b \
  --http-url "http://127.0.0.1:$PROVIDER_B_HTTP_PORT" \
  --registry-router-endpoint "$REG_ROUTER" \
  --channel-endpoint "$API_B" \
  --manual-client-endpoint "$API_B" \
  --route-endpoint "$ROUTE_B" \
  --route-peer "$ROUTE_A" \
  --evidence-file "$LOG_DIR/api-b.evidence.log" \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$PROVIDER_B_HTTP_PORT" api-b

start_server workflow-a "$PROVIDER_PROJECT" \
  --rid workflow-a \
  --http-url "http://127.0.0.1:$WORKFLOW_HTTP_PORT" \
  --registry-router-endpoint "$REG_ROUTER" \
  --workflow-endpoint "$WORKFLOW" \
  --evidence-file "$LOG_DIR/workflow-a.evidence.log" \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$WORKFLOW_HTTP_PORT" workflow-a

start_server direct-consumer "$CONSUMER_PROJECT" \
  --http-url "http://127.0.0.1:$CONSUMER_HTTP_PORT" \
  --provider-endpoint "$API_A" \
  --provider-endpoint "$API_B" \
  --trace-label direct-consumer \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$CONSUMER_HTTP_PORT" direct-consumer

start_server single-consumer "$CONSUMER_PROJECT" \
  --http-url "http://127.0.0.1:$SINGLE_CONSUMER_HTTP_PORT" \
  --provider-endpoint "$API_A" \
  --trace-label single-consumer \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$SINGLE_CONSUMER_HTTP_PORT" single-consumer

start_server discovery-consumer "$CONSUMER_PROJECT" \
  --http-url "http://127.0.0.1:$DISCOVERY_CONSUMER_HTTP_PORT" \
  --registry-router-endpoint "$REG_ROUTER" \
  --trace-label discovery-consumer \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$DISCOVERY_CONSUMER_HTTP_PORT" discovery-consumer

start_server backpressure-consumer "$CONSUMER_PROJECT" \
  --http-url "http://127.0.0.1:$BACKPRESSURE_CONSUMER_HTTP_PORT" \
  --provider-endpoint "$API_A" \
  --low-hwm true \
  --trace-label backpressure-consumer \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$BACKPRESSURE_CONSUMER_HTTP_PORT" backpressure-consumer

dotnet run --project "$CLIENT_PROJECT" -- \
  --registry-url "http://127.0.0.1:$REG_HTTP_PORT" \
  --provider-a-url "http://127.0.0.1:$PROVIDER_A_HTTP_PORT" \
  --provider-b-url "http://127.0.0.1:$PROVIDER_B_HTTP_PORT" \
  --workflow-url "http://127.0.0.1:$WORKFLOW_HTTP_PORT" \
  --direct-consumer-url "http://127.0.0.1:$CONSUMER_HTTP_PORT" \
  --single-consumer-url "http://127.0.0.1:$SINGLE_CONSUMER_HTTP_PORT" \
  --discovery-consumer-url "http://127.0.0.1:$DISCOVERY_CONSUMER_HTTP_PORT" \
  --backpressure-consumer-url "http://127.0.0.1:$BACKPRESSURE_CONSUMER_HTTP_PORT" \
  --registry-project "$REGISTRY_PROJECT" \
  --provider-project "$PROVIDER_PROJECT" \
  --log-dir "$LOG_DIR" \
  --scenario "$SCENARIO" \
  >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"

cat "$LOG_DIR/client.stdout.log"
