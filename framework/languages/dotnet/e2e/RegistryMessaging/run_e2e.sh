#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"

SERVER_PROJECT="$ROOT_DIR/Server/RegistryMessaging.Server.csproj"
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
REG_PUB_PORT="$(pick_port)"
REG_ROUTER_PORT="$(pick_port)"
API_A_PORT="$(pick_port)"
API_B_PORT="$(pick_port)"
ROUTE_A_PORT="$(pick_port)"
ROUTE_B_PORT="$(pick_port)"
DEALER_A_PORT="$(pick_port)"
DEALER_B_PORT="$(pick_port)"
CLIENT_ROUTE_PORT="$(pick_port)"

REG_PUB="tcp://127.0.0.1:$REG_PUB_PORT"
REG_ROUTER="tcp://127.0.0.1:$REG_ROUTER_PORT"
API_A="tcp://127.0.0.1:$API_A_PORT"
API_B="tcp://127.0.0.1:$API_B_PORT"
ROUTE_A="tcp://127.0.0.1:$ROUTE_A_PORT"
ROUTE_B="tcp://127.0.0.1:$ROUTE_B_PORT"
DEALER_A="tcp://127.0.0.1:$DEALER_A_PORT"
DEALER_B="tcp://127.0.0.1:$DEALER_B_PORT"
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
  shift
  dotnet run --project "$SERVER_PROJECT" -- "$@" \
    >"$LOG_DIR/$name.stdout.log" 2>"$LOG_DIR/$name.stderr.log" &
  pids+=("$!")
}

echo "log_dir=$LOG_DIR"

start_server registry \
  --role registry \
  --rid registry \
  --http-url "http://127.0.0.1:$REG_HTTP_PORT" \
  --registry-pub-endpoint "$REG_PUB" \
  --registry-router-endpoint "$REG_ROUTER" \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$REG_HTTP_PORT" registry

start_server api-a \
  --role provider \
  --rid api-a \
  --http-url "http://127.0.0.1:$PROVIDER_A_HTTP_PORT" \
  --registry-router-endpoint "$REG_ROUTER" \
  --channel-endpoint "$API_A" \
  --route-endpoint "$ROUTE_A" \
  --route-peer "$ROUTE_B" \
  --dealer-endpoint "$DEALER_A" \
  --evidence-file "$LOG_DIR/api-a.evidence.log" \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$PROVIDER_A_HTTP_PORT" api-a

start_server api-b \
  --role provider \
  --rid api-b \
  --http-url "http://127.0.0.1:$PROVIDER_B_HTTP_PORT" \
  --registry-router-endpoint "$REG_ROUTER" \
  --channel-endpoint "$API_B" \
  --route-endpoint "$ROUTE_B" \
  --route-peer "$ROUTE_A" \
  --dealer-endpoint "$DEALER_B" \
  --evidence-file "$LOG_DIR/api-b.evidence.log" \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$PROVIDER_B_HTTP_PORT" api-b

dotnet run --project "$CLIENT_PROJECT" -- \
  --registry-router-endpoint "$REG_ROUTER" \
  --provider-a-endpoint "$API_A" \
  --provider-b-endpoint "$API_B" \
  --provider-a-route-endpoint "$ROUTE_A" \
  --provider-b-route-endpoint "$ROUTE_B" \
  --provider-a-dealer-endpoint "$DEALER_A" \
  --provider-b-dealer-endpoint "$DEALER_B" \
  --client-route-endpoint "$CLIENT_ROUTE" \
  --provider-a-evidence-url "http://127.0.0.1:$PROVIDER_A_HTTP_PORT/evidence" \
  --provider-b-evidence-url "http://127.0.0.1:$PROVIDER_B_HTTP_PORT/evidence" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"

cat "$LOG_DIR/client.stdout.log"
