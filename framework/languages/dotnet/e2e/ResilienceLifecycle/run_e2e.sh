#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"

REGISTRY_PROJECT="$ROOT_DIR/Server/Registry/ResilienceLifecycle.Registry.csproj"
PROVIDER_PROJECT="$ROOT_DIR/Server/Provider/ResilienceLifecycle.Provider.csproj"
DRIVER_PROJECT="$ROOT_DIR/Server/Driver/ResilienceLifecycle.Driver.csproj"
CLIENT_PROJECT="$ROOT_DIR/Client/ResilienceLifecycle.Client.csproj"

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
API_A_HTTP_PORT="$(pick_port)"
API_B_HTTP_PORT="$(pick_port)"
DRIVER_HTTP_PORT="$(pick_port)"
REG_PUB_PORT="$(pick_port)"
REG_ROUTER_PORT="$(pick_port)"
API_A_PORT="$(pick_port)"
API_B_PORT="$(pick_port)"

REG_PUB="tcp://127.0.0.1:$REG_PUB_PORT"
REG_ROUTER="tcp://127.0.0.1:$REG_ROUTER_PORT"
API_A="tcp://127.0.0.1:$API_A_PORT"
API_B="tcp://127.0.0.1:$API_B_PORT"
API_A_URL="http://127.0.0.1:$API_A_HTTP_PORT"
API_B_URL="http://127.0.0.1:$API_B_HTTP_PORT"

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
  ZLINK_E2E_RID="$name" dotnet run --project "$project" -- "$@" \
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
  --http-url "$API_A_URL" \
  --registry-router-endpoint "$REG_ROUTER" \
  --channel-endpoint "$API_A" \
  --evidence-file "$LOG_DIR/api-a.evidence.log" \
  --log-dir "$LOG_DIR"
wait_health "$API_A_URL" api-a

start_server api-b "$PROVIDER_PROJECT" \
  --rid api-b \
  --http-url "$API_B_URL" \
  --registry-router-endpoint "$REG_ROUTER" \
  --channel-endpoint "$API_B" \
  --evidence-file "$LOG_DIR/api-b.evidence.log" \
  --log-dir "$LOG_DIR"
wait_health "$API_B_URL" api-b

start_server driver "$DRIVER_PROJECT" \
  --driver-url "http://127.0.0.1:$DRIVER_HTTP_PORT" \
  --registry-router-endpoint "$REG_ROUTER" \
  --registry-url "http://127.0.0.1:$REG_HTTP_PORT" \
  --registry-pub-endpoint "$REG_PUB" \
  --provider-a-url "$API_A_URL" \
  --provider-b-url "$API_B_URL" \
  --provider-a-endpoint "$API_A" \
  --provider-b-endpoint "$API_B" \
  --provider-a-evidence-file "$LOG_DIR/api-a.evidence.log" \
  --provider-b-evidence-file "$LOG_DIR/api-b.evidence.log" \
  --registry-project "$REGISTRY_PROJECT" \
  --provider-project "$PROVIDER_PROJECT" \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$DRIVER_HTTP_PORT" driver

dotnet run --project "$CLIENT_PROJECT" -- \
  --driver-url "http://127.0.0.1:$DRIVER_HTTP_PORT" \
  >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"

cat "$LOG_DIR/client.stdout.log"
