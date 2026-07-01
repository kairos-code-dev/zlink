#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"
SCENARIO="${1:-all}"
LOCAL_READINESS_TIMEOUT_SECONDS=3
LOCAL_READINESS_POLL_SECONDS=0.1
ROUTE_SETTLE_SECONDS=5
SCENARIO_SETTLE_SECONDS=3
HTTP_PROBE_TIMEOUT_SECONDS=3
LOCAL_READINESS_ATTEMPTS="$(
  python3 - "$LOCAL_READINESS_TIMEOUT_SECONDS" "$LOCAL_READINESS_POLL_SECONDS" <<'PY'
import math
import sys

timeout = float(sys.argv[1])
poll = float(sys.argv[2])
print(max(1, math.ceil(timeout / poll)))
PY
)"

REGISTRY_PROJECT="$ROOT_DIR/Server/Registry/ResilienceLifecycle.Registry.csproj"
PROVIDER_PROJECT="$ROOT_DIR/Server/Provider/ResilienceLifecycle.Provider.csproj"
CONSUMER_PROJECT="$ROOT_DIR/Server/Consumer/ResilienceLifecycle.Consumer.csproj"
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
CONSUMER_HTTP_PORT="$(pick_port)"
REG_PUB_PORT="$(pick_port)"
REG_ROUTER_PORT="$(pick_port)"
API_A_PORT="$(pick_port)"
API_B_PORT="$(pick_port)"
API_B_REMAP_HTTP_PORT="$(pick_port)"
API_B_REMAP_PORT="$(pick_port)"
API_B_GREEN_HTTP_PORT="$(pick_port)"
API_B_GREEN_PORT="$(pick_port)"

REG_PUB="tcp://127.0.0.1:$REG_PUB_PORT"
REG_ROUTER="tcp://127.0.0.1:$REG_ROUTER_PORT"
API_A="tcp://127.0.0.1:$API_A_PORT"
API_B="tcp://127.0.0.1:$API_B_PORT"
API_A_URL="http://127.0.0.1:$API_A_HTTP_PORT"
API_B_URL="http://127.0.0.1:$API_B_HTTP_PORT"
API_B_REMAP_URL="http://127.0.0.1:$API_B_REMAP_HTTP_PORT"
API_B_REMAP="tcp://127.0.0.1:$API_B_REMAP_PORT"
API_B_GREEN_URL="http://127.0.0.1:$API_B_GREEN_HTTP_PORT"
API_B_GREEN="tcp://127.0.0.1:$API_B_GREEN_PORT"

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
  local deadline_ns
  deadline_ns="$(
    python3 - "$LOCAL_READINESS_TIMEOUT_SECONDS" <<PY
import sys
import time

timeout = float(sys.argv[1])
print(time.monotonic_ns() + int(timeout * 1_000_000_000))
PY
  )"
  while true; do
    local probe_timeout
    probe_timeout="$(
      python3 - "$deadline_ns" "$HTTP_PROBE_TIMEOUT_SECONDS" <<PY
import sys
import time

deadline_ns = int(sys.argv[1])
probe_timeout = float(sys.argv[2])
remaining = (deadline_ns - time.monotonic_ns()) / 1_000_000_000
if remaining <= 0:
    print("0")
else:
    print(f"{min(probe_timeout, remaining):.3f}")
PY
    )"
    if [[ "$probe_timeout" == "0" ]]; then
      break
    fi
    if curl --max-time "$probe_timeout" \
      --connect-timeout "$probe_timeout" \
      -fsS "$url/health" >/dev/null 2>&1; then
      return 0
    fi
    python3 - "$deadline_ns" "$LOCAL_READINESS_POLL_SECONDS" <<PY
import sys
import time

deadline_ns = int(sys.argv[1])
poll = float(sys.argv[2])
remaining = (deadline_ns - time.monotonic_ns()) / 1_000_000_000
if remaining > 0:
    time.sleep(min(poll, remaining))
PY
  done
  echo "Timed out waiting ${LOCAL_READINESS_TIMEOUT_SECONDS}s for $name at $url" >&2
  return 1
}

start_server() {
  local name="$1"
  local project="$2"
  shift
  shift
  ZLINK_E2E_RID="$name" dotnet run --no-build --project "$project" -- "$@" \
    >"$LOG_DIR/$name.stdout.log" 2>"$LOG_DIR/$name.stderr.log" &
  pids+=("$!")
}

echo "log_dir=$LOG_DIR"
dotnet build "$REGISTRY_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$PROVIDER_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$CONSUMER_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$CLIENT_PROJECT" --maxcpucount:1 >/dev/null

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

start_server consumer "$CONSUMER_PROJECT" \
  --http-url "http://127.0.0.1:$CONSUMER_HTTP_PORT" \
  --registry-router-endpoint "$REG_ROUTER" \
  --log-dir "$LOG_DIR"
wait_health "http://127.0.0.1:$CONSUMER_HTTP_PORT" consumer

sleep "$ROUTE_SETTLE_SECONDS"

dotnet run --no-build --project "$CLIENT_PROJECT" -- \
  --consumer-url "http://127.0.0.1:$CONSUMER_HTTP_PORT" \
  --registry-url "http://127.0.0.1:$REG_HTTP_PORT" \
  --registry-pub-endpoint "$REG_PUB" \
  --registry-router-endpoint "$REG_ROUTER" \
  --provider-a-url "$API_A_URL" \
  --provider-a-endpoint "$API_A" \
  --provider-a-evidence-file "$LOG_DIR/api-a.evidence.log" \
  --provider-b-url "$API_B_URL" \
  --provider-b-endpoint "$API_B" \
  --provider-b-evidence-file "$LOG_DIR/api-b.evidence.log" \
  --provider-b-remap-url "$API_B_REMAP_URL" \
  --provider-b-remap-endpoint "$API_B_REMAP" \
  --provider-b-green-url "$API_B_GREEN_URL" \
  --provider-b-green-endpoint "$API_B_GREEN" \
  --registry-project "$REGISTRY_PROJECT" \
  --provider-project "$PROVIDER_PROJECT" \
  --log-dir "$LOG_DIR" \
  --scenario "$SCENARIO" \
  >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"

cat "$LOG_DIR/client.stdout.log"
