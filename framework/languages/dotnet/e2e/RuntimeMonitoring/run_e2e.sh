#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"
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

REGISTRY_PROJECT="$ROOT_DIR/Server/Registry/RuntimeMonitoring.Registry.csproj"
SERVICE_PROJECT="$ROOT_DIR/Server/Service/RuntimeMonitoring.Service.csproj"
FILTERED_SERVICE_PROJECT="$ROOT_DIR/Server/FilteredService/RuntimeMonitoring.FilteredService.csproj"
THROWING_SERVICE_PROJECT="$ROOT_DIR/Server/ThrowingService/RuntimeMonitoring.ThrowingService.csproj"
TRIGGER_PROJECT="$ROOT_DIR/Server/Trigger/RuntimeMonitoring.Trigger.csproj"
CLIENT_PROJECT="$ROOT_DIR/Client/RuntimeMonitoring.Client.csproj"

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
SVC_HTTP_PORT="$(pick_port)"
SVC_B_HTTP_PORT="$(pick_port)"
THROW_HTTP_PORT="$(pick_port)"
TRIGGER_HTTP_PORT="$(pick_port)"
REG_PUB_PORT="$(pick_port)"
REG_ROUTER_PORT="$(pick_port)"
CHANNEL_PORT="$(pick_port)"
CHANNEL_B_PORT="$(pick_port)"
THROW_CHANNEL_PORT="$(pick_port)"
SPOT_ROUTER_PORT="$(pick_port)"
SPOT_PUB_PORT="$(pick_port)"

REG_URL="http://127.0.0.1:$REG_HTTP_PORT"
SVC_URL="http://127.0.0.1:$SVC_HTTP_PORT"
SVC_B_URL="http://127.0.0.1:$SVC_B_HTTP_PORT"
THROW_URL="http://127.0.0.1:$THROW_HTTP_PORT"
REG_PUB="tcp://127.0.0.1:$REG_PUB_PORT"
REG_ROUTER="tcp://127.0.0.1:$REG_ROUTER_PORT"
CHANNEL_ENDPOINT="tcp://127.0.0.1:$CHANNEL_PORT"
CHANNEL_B_ENDPOINT="tcp://127.0.0.1:$CHANNEL_B_PORT"
THROW_CHANNEL_ENDPOINT="tcp://127.0.0.1:$THROW_CHANNEL_PORT"
SPOT_ROUTER_ENDPOINT="tcp://127.0.0.1:$SPOT_ROUTER_PORT"
SPOT_PUB_ENDPOINT="tcp://127.0.0.1:$SPOT_PUB_PORT"

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

echo "log_dir=$LOG_DIR"
dotnet build "$REGISTRY_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$SERVICE_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$FILTERED_SERVICE_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$THROWING_SERVICE_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$TRIGGER_PROJECT" --maxcpucount:1 >/dev/null
dotnet build "$CLIENT_PROJECT" --maxcpucount:1 >/dev/null

ZLINK_E2E_RID="registry" dotnet run --no-build --project "$REGISTRY_PROJECT" -- \
  --rid registry \
  --http-url "$REG_URL" \
  --registry-pub-endpoint "$REG_PUB" \
  --registry-router-endpoint "$REG_ROUTER" \
  --evidence-file "$LOG_DIR/registry.evidence.log" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/registry.stdout.log" 2>"$LOG_DIR/registry.stderr.log" &
pids+=("$!")
wait_health "$REG_URL" registry

ZLINK_E2E_RID="svc-a" dotnet run --no-build --project "$SERVICE_PROJECT" -- \
  --rid svc-a \
  --http-url "$SVC_URL" \
  --registry-router-endpoint "$REG_ROUTER" \
  --channel-endpoint "$CHANNEL_ENDPOINT" \
  --spot-router-endpoint "$SPOT_ROUTER_ENDPOINT" \
  --spot-pub-endpoint "$SPOT_PUB_ENDPOINT" \
  --evidence-file "$LOG_DIR/svc-a.evidence.log" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/svc-a.stdout.log" 2>"$LOG_DIR/svc-a.stderr.log" &
pids+=("$!")
wait_health "$SVC_URL" svc-a

ZLINK_E2E_RID="svc-b" dotnet run --no-build --project "$FILTERED_SERVICE_PROJECT" -- \
  --rid svc-b \
  --http-url "$SVC_B_URL" \
  --registry-router-endpoint "$REG_ROUTER" \
  --channel-endpoint "$CHANNEL_B_ENDPOINT" \
  --evidence-file "$LOG_DIR/svc-b.evidence.log" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/svc-b.stdout.log" 2>"$LOG_DIR/svc-b.stderr.log" &
pids+=("$!")
wait_health "$SVC_B_URL" svc-b

ZLINK_E2E_RID="svc-throw" ZLINK_DEBUG_FRAMEWORK_TASKS=1 dotnet run --no-build --project "$THROWING_SERVICE_PROJECT" -- \
  --rid svc-throw \
  --http-url "$THROW_URL" \
  --registry-router-endpoint "$REG_ROUTER" \
  --channel-endpoint "$THROW_CHANNEL_ENDPOINT" \
  --evidence-file "$LOG_DIR/svc-throw.evidence.log" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/svc-throw.stdout.log" 2>"$LOG_DIR/svc-throw.stderr.log" &
pids+=("$!")
wait_health "$THROW_URL" svc-throw

ZLINK_E2E_RID="trigger" dotnet run --no-build --project "$TRIGGER_PROJECT" -- \
  --http-url "http://127.0.0.1:$TRIGGER_HTTP_PORT" \
  --registry-url "$REG_URL" \
  --registry-router-endpoint "$REG_ROUTER" \
  --service-channel-endpoint "$CHANNEL_ENDPOINT" \
  --service-b-url "$SVC_B_URL" \
  --service-b-channel-endpoint "$CHANNEL_B_ENDPOINT" \
  --throw-channel-endpoint "$THROW_CHANNEL_ENDPOINT" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/trigger.stdout.log" 2>"$LOG_DIR/trigger.stderr.log" &
pids+=("$!")
wait_health "http://127.0.0.1:$TRIGGER_HTTP_PORT" trigger

sleep "$ROUTE_SETTLE_SECONDS"

dotnet run --no-build --project "$CLIENT_PROJECT" -- \
  --trigger-url "http://127.0.0.1:$TRIGGER_HTTP_PORT" \
  --registry-router-endpoint "$REG_ROUTER" \
  --registry-url "$REG_URL" \
  --service-url "$SVC_URL" \
  --service-channel-endpoint "$CHANNEL_ENDPOINT" \
  --service-b-url "$SVC_B_URL" \
  --service-b-channel-endpoint "$CHANNEL_B_ENDPOINT" \
  --throw-service-url "$THROW_URL" \
  --throw-channel-endpoint "$THROW_CHANNEL_ENDPOINT" \
  --filtered-service-project "$FILTERED_SERVICE_PROJECT" \
  --log-dir "$LOG_DIR" \
  >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"

cat "$LOG_DIR/client.stdout.log"
