#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FRAMEWORK_DIR="$(cd "$ROOT_DIR/../.." && pwd)"
source "$ROOT_DIR/../redis-common.sh"
BUILD_DIR="${ZLINK_CPP_E2E_BUILD_DIR:-$FRAMEWORK_DIR/build-redis-vcpkg}"
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
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"

echo "log_dir=$LOG_DIR"

read -r CHANNEL CHANNEL_FILTERED CHANNEL_THROW SPOT_ROUTER_SERVICE SPOT_ROUTER_FILTERED SPOT_ROUTER_THROW SPOT_PUB_SERVICE SPOT_PUB_FILTERED SPOT_PUB_THROW HTTP_SERVICE HTTP_FILTERED HTTP_THROW HTTP_TRIGGER <<<"$(python3 - <<'PY'
import socket
sockets = []
ports = []
for _ in range(13):
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    sockets.append(s)
    ports.append(s.getsockname()[1])
print(" ".join(f"tcp://127.0.0.1:{p}" for p in ports[:9]), end=" ")
print(" ".join(f"http://127.0.0.1:{p}" for p in ports[9:13]))
for s in sockets:
    s.close()
PY
)"

cmake -S "$FRAMEWORK_DIR" -B "$BUILD_DIR" >/dev/null
cmake --build "$BUILD_DIR" --target \
  zlink_cpp_e2e_runtime_monitoring_service \
  zlink_cpp_e2e_runtime_monitoring_filtered_service \
  zlink_cpp_e2e_runtime_monitoring_throwing_service \
  zlink_cpp_e2e_runtime_monitoring_trigger \
  zlink_cpp_e2e_runtime_monitoring_client

SERVICE="$BUILD_DIR/zlink_cpp_e2e_runtime_monitoring_service"
FILTERED_SERVICE="$BUILD_DIR/zlink_cpp_e2e_runtime_monitoring_filtered_service"
THROWING_SERVICE="$BUILD_DIR/zlink_cpp_e2e_runtime_monitoring_throwing_service"
TRIGGER="$BUILD_DIR/zlink_cpp_e2e_runtime_monitoring_trigger"
CLIENT="$BUILD_DIR/zlink_cpp_e2e_runtime_monitoring_client"
read -r REDIS_CONTAINER redis_port < <(
  zlink_redis_start_scoped "zlink-redis-cpp-e2e-runtimemonitoring" "redis:7-alpine"
)
REDIS_ENDPOINT="127.0.0.1:${redis_port}"
REDIS_KEY_PREFIX="zlink:cpp:runtime-monitoring:${RUN_ID}"
PIDS=()

cleanup() {
  local code=$?
  for pid in "${PIDS[@]:-}"; do
    if kill -0 "$pid" >/dev/null 2>&1; then
      kill "$pid" >/dev/null 2>&1 || true
    fi
  done
  wait >/dev/null 2>&1 || true
  docker rm -f "$REDIS_CONTAINER" >/dev/null 2>&1 || true
  if [[ $code -ne 0 ]]; then
    echo "E2E failed. Logs: $LOG_DIR" >&2
  fi
  exit "$code"
}
trap cleanup EXIT

for _ in $(seq 1 80); do
  if docker exec "$REDIS_CONTAINER" redis-cli ping >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done

port_of() {
  local endpoint="$1"
  echo "${endpoint##*:}"
}

wait_port() {
  local name="$1"
  local endpoint="$2"
  local port
  port="$(port_of "$endpoint")"
  for _ in $(seq 1 "$LOCAL_READINESS_ATTEMPTS"); do
    if (echo >"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep "$LOCAL_READINESS_POLL_SECONDS"
  done
  echo "Timed out waiting ${LOCAL_READINESS_TIMEOUT_SECONDS}s for $name at $endpoint" >&2
  return 1
}

wait_port_closed() {
  local name="$1"
  local endpoint="$2"
  local port
  port="$(port_of "$endpoint")"
  for _ in $(seq 1 "$LOCAL_READINESS_ATTEMPTS"); do
    if ! (echo >"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep "$LOCAL_READINESS_POLL_SECONDS"
  done
  echo "Timed out waiting ${LOCAL_READINESS_TIMEOUT_SECONDS}s for $name to stop at $endpoint" >&2
  return 1
}

stop_service_http() {
  local endpoint="$1"
  python3 - "$endpoint" "$HTTP_PROBE_TIMEOUT_SECONDS" <<'PY'
import sys
import urllib.request

base = sys.argv[1]
timeout_seconds = float(sys.argv[2])
request = urllib.request.Request(f"{base}/shutdown", data=b"", method="POST")
try:
    urllib.request.urlopen(request, timeout=timeout_seconds).read()
except Exception:
    pass
PY
}

ZLINK_CPP_E2E_RID=svc-a \
ZLINK_CPP_E2E_HTTP_ENDPOINT="$HTTP_SERVICE" \
ZLINK_CPP_E2E_REDIS_ENDPOINT="$REDIS_ENDPOINT" \
ZLINK_CPP_E2E_REDIS_KEY_PREFIX="$REDIS_KEY_PREFIX" \
ZLINK_CPP_E2E_CHANNEL_ENDPOINT="$CHANNEL" \
ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_ROUTER_SERVICE" \
ZLINK_CPP_E2E_SPOT_PUB_ENDPOINT="$SPOT_PUB_SERVICE" \
ZLINK_CPP_E2E_EVIDENCE_FILE="$LOG_DIR/service.evidence.log" \
ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
  "$SERVICE" >"$LOG_DIR/service.stdout.log" 2>"$LOG_DIR/service.stderr.log" &
PIDS+=("$!")
wait_port service "$HTTP_SERVICE"

ZLINK_CPP_E2E_RID=svc-b \
ZLINK_CPP_E2E_HTTP_ENDPOINT="$HTTP_FILTERED" \
ZLINK_CPP_E2E_REDIS_ENDPOINT="$REDIS_ENDPOINT" \
ZLINK_CPP_E2E_REDIS_KEY_PREFIX="$REDIS_KEY_PREFIX" \
ZLINK_CPP_E2E_CHANNEL_ENDPOINT="$CHANNEL_FILTERED" \
ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_ROUTER_FILTERED" \
ZLINK_CPP_E2E_SPOT_PUB_ENDPOINT="$SPOT_PUB_FILTERED" \
ZLINK_CPP_E2E_EVIDENCE_FILE="$LOG_DIR/filtered.evidence.log" \
ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
  "$FILTERED_SERVICE" >"$LOG_DIR/filtered.stdout.log" 2>"$LOG_DIR/filtered.stderr.log" &
FILTERED_PID="$!"
PIDS+=("$FILTERED_PID")
wait_port filtered-service "$HTTP_FILTERED"

ZLINK_CPP_E2E_RID=svc-throw \
ZLINK_CPP_E2E_HTTP_ENDPOINT="$HTTP_THROW" \
ZLINK_CPP_E2E_REDIS_ENDPOINT="$REDIS_ENDPOINT" \
ZLINK_CPP_E2E_REDIS_KEY_PREFIX="$REDIS_KEY_PREFIX" \
ZLINK_CPP_E2E_CHANNEL_ENDPOINT="$CHANNEL_THROW" \
ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_ROUTER_THROW" \
ZLINK_CPP_E2E_SPOT_PUB_ENDPOINT="$SPOT_PUB_THROW" \
ZLINK_CPP_E2E_EVIDENCE_FILE="$LOG_DIR/throw.evidence.log" \
ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
  "$THROWING_SERVICE" >"$LOG_DIR/throw.stdout.log" 2>"$LOG_DIR/throw.stderr.log" &
PIDS+=("$!")
wait_port throwing-service "$HTTP_THROW"

ZLINK_CPP_E2E_RID=trigger \
ZLINK_CPP_E2E_HTTP_ENDPOINT="$HTTP_TRIGGER" \
ZLINK_CPP_E2E_REDIS_ENDPOINT="$REDIS_ENDPOINT" \
ZLINK_CPP_E2E_REDIS_KEY_PREFIX="$REDIS_KEY_PREFIX" \
ZLINK_CPP_E2E_SERVICE_CHANNEL_ENDPOINT="$CHANNEL" \
ZLINK_CPP_E2E_SERVICE_B_CHANNEL_ENDPOINT="$CHANNEL_FILTERED" \
ZLINK_CPP_E2E_THROW_CHANNEL_ENDPOINT="$CHANNEL_THROW" \
ZLINK_CPP_E2E_EVIDENCE_FILE="$LOG_DIR/trigger.evidence.log" \
ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
  "$TRIGGER" >"$LOG_DIR/trigger.stdout.log" 2>"$LOG_DIR/trigger.stderr.log" &
PIDS+=("$!")
wait_port trigger "$HTTP_TRIGGER"

sleep "$ROUTE_SETTLE_SECONDS"

ZLINK_CPP_E2E_SERVICE_URL="$HTTP_SERVICE" \
ZLINK_CPP_E2E_FILTERED_SERVICE_URL="$HTTP_FILTERED" \
ZLINK_CPP_E2E_THROW_SERVICE_URL="$HTTP_THROW" \
ZLINK_CPP_E2E_TRIGGER_URL="$HTTP_TRIGGER" \
ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
  "$CLIENT" \
  >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"

cat "$LOG_DIR/client.stdout.log"
grep -q "scenario MON-A1 passed" "$LOG_DIR/client.stdout.log"
grep -q "scenario MON-A2 passed" "$LOG_DIR/client.stdout.log"
grep -q "scenario MON-A3 passed" "$LOG_DIR/client.stdout.log"
grep -q "scenario MON-A5 passed" "$LOG_DIR/client.stdout.log"
grep -q "scenario MON-A4 passed" "$LOG_DIR/client.stdout.log"
grep -q "scenario MON-B1 passed" "$LOG_DIR/client.stdout.log"
grep -q "scenario MON-C1 passed" "$LOG_DIR/client.stdout.log"
grep -q "scenario MON-B2 passed" "$LOG_DIR/client.stdout.log"
grep -q "runtime-monitoring client result=passed" "$LOG_DIR/client.stdout.log"
grep -q "monitoring-event-dispatch" "$LOG_DIR/throw.stderr.log"
grep -q "monitoring dispatch failure for e2e" "$LOG_DIR/throw.stderr.log"
grep -q "message flow" "$LOG_DIR/svc-a-flow.log"
grep -q "message flow" "$LOG_DIR/svc-b-flow.log"
grep -q "message flow" "$LOG_DIR/trigger-service-a-mon-a1-flow.log"
grep -q "message flow" "$LOG_DIR/trigger-service-b-mon-b1-flow.log"

stop_service_http "$HTTP_FILTERED"
wait "$FILTERED_PID" >/dev/null 2>&1 || true
wait_port_closed filtered-service "$HTTP_FILTERED"

ZLINK_CPP_E2E_RID=svc-b \
ZLINK_CPP_E2E_HTTP_ENDPOINT="$HTTP_FILTERED" \
ZLINK_CPP_E2E_REDIS_ENDPOINT="$REDIS_ENDPOINT" \
ZLINK_CPP_E2E_REDIS_KEY_PREFIX="$REDIS_KEY_PREFIX" \
ZLINK_CPP_E2E_CHANNEL_ENDPOINT="$CHANNEL_FILTERED" \
ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$SPOT_ROUTER_FILTERED" \
ZLINK_CPP_E2E_SPOT_PUB_ENDPOINT="$SPOT_PUB_FILTERED" \
ZLINK_CPP_E2E_EVIDENCE_FILE="$LOG_DIR/filtered-restart.evidence.log" \
ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
  "$FILTERED_SERVICE" >"$LOG_DIR/filtered-restart.stdout.log" 2>"$LOG_DIR/filtered-restart.stderr.log" &
FILTERED_RESTART_PID="$!"
PIDS+=("$FILTERED_RESTART_PID")
wait_port filtered-service-restart "$HTTP_FILTERED"

ZLINK_CPP_E2E_SCENARIO=mon-d1 \
ZLINK_CPP_E2E_SERVICE_URL="$HTTP_SERVICE" \
ZLINK_CPP_E2E_FILTERED_SERVICE_URL="$HTTP_FILTERED" \
ZLINK_CPP_E2E_TRIGGER_URL="$HTTP_TRIGGER" \
ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
  "$CLIENT" \
  >"$LOG_DIR/client-d1.stdout.log" 2>"$LOG_DIR/client-d1.stderr.log"

cat "$LOG_DIR/client-d1.stdout.log"
grep -q "scenario MON-D1 passed" "$LOG_DIR/client-d1.stdout.log"
grep -q "message flow" "$LOG_DIR/trigger-service-b-mon-d1-flow.log"

echo "runtime-monitoring e2e result=passed"
