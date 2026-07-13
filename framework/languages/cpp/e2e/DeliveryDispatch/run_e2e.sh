#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source "$SCRIPT_DIR/../redis-common.sh"
SCENARIO="${*:-all}"
SCENARIO="${SCENARIO// /,}"
case "$SCENARIO" in
  all|DD-A1|dd-a1|DeliveryDispatch|deliverydispatch) ;;
  *)
    echo "Unsupported DeliveryDispatch scenario: $SCENARIO" >&2
    exit 2
    ;;
esac
export DELIVERYDISPATCH_LOG_DIR="${DELIVERYDISPATCH_LOG_DIR:-${SCRIPT_DIR}/logs}"
mkdir -p "$DELIVERYDISPATCH_LOG_DIR"
rm -f "$DELIVERYDISPATCH_LOG_DIR"/*.log
BUILD_DIR="${ZLINK_CPP_BUILD_DIR:-$CPP_ROOT/build}"
BIN_DIR="$BUILD_DIR"
if [[ ! -x "$BIN_DIR/zlink_cpp_e2e_delivery_dispatch_client" && -x "$BIN_DIR/linux-ninja-debug/zlink_cpp_e2e_delivery_dispatch_client" ]]; then
  BIN_DIR="$BIN_DIR/linux-ninja-debug"
fi
LOCAL_READINESS_TIMEOUT_SECONDS=3
LOCAL_READINESS_POLL_SECONDS=0.1
ROUTE_SETTLE_SECONDS=5
SCENARIO_SETTLE_SECONDS=3
HTTP_PROBE_TIMEOUT_SECONDS=3
PROCESS_SHUTDOWN_TIMEOUT_SECONDS=2
PROCESS_SHUTDOWN_POLL_SECONDS=0.05
REDIS_CONTAINER_NAME=""
REDIS_CONTAINER_OWNED=0
LOCAL_READINESS_ATTEMPTS="$(
  python3 - "$LOCAL_READINESS_TIMEOUT_SECONDS" "$LOCAL_READINESS_POLL_SECONDS" <<'PY'
import math
import sys

timeout = float(sys.argv[1])
poll = float(sys.argv[2])
print(max(1, math.ceil(timeout / poll)))
PY
)"
PROCESS_SHUTDOWN_ATTEMPTS="$(
  python3 - "$PROCESS_SHUTDOWN_TIMEOUT_SECONDS" "$PROCESS_SHUTDOWN_POLL_SECONDS" <<'PY'
import math
import sys

timeout = float(sys.argv[1])
poll = float(sys.argv[2])
print(max(1, math.ceil(timeout / poll)))
PY
)"

PIDS=()
LOG_DIR="$DELIVERYDISPATCH_LOG_DIR/last-run"
rm -rf "$LOG_DIR"
mkdir -p "$LOG_DIR"
cleanup() {
  local code=$?
  local cleanup_failed=0
  local status
  for pid in "${PIDS[@]}"; do
    if kill -0 "${pid}" >/dev/null 2>&1; then
      kill "${pid}" >/dev/null 2>&1 || true
      for _ in $(seq 1 "$PROCESS_SHUTDOWN_ATTEMPTS"); do
        if ! kill -0 "${pid}" >/dev/null 2>&1; then
          break
        fi
        sleep "$PROCESS_SHUTDOWN_POLL_SECONDS"
      done
      if kill -0 "${pid}" >/dev/null 2>&1; then
        echo "forced cleanup process ${pid}" >&2
        kill -9 "${pid}" >/dev/null 2>&1 || true
        cleanup_failed=1
      fi
    fi
    set +e
    wait "${pid}" >/dev/null 2>&1
    status=$?
    set -e
    if [[ "$status" != "0" && "$status" != "127" && "$status" != "130" && "$status" != "143" ]]; then
      echo "cleanup process ${pid} exited unexpectedly with status ${status}" >&2
      cleanup_failed=1
    fi
  done
  if [[ -n "$REDIS_CONTAINER_NAME" && "$REDIS_CONTAINER_OWNED" == "1" ]]; then
    docker rm -fv "$REDIS_CONTAINER_NAME" >/dev/null 2>&1 || true
  fi
  if [[ "$cleanup_failed" -ne 0 && "$code" -eq 0 ]]; then
    code=1
  fi
  exit "$code"
}
trap cleanup EXIT

dump_logs() {
  echo "===== DeliveryDispatch logs: $LOG_DIR" >&2
  for log in "$LOG_DIR"/*.log "$DELIVERYDISPATCH_LOG_DIR"/flow-*.log; do
    if [[ -f "$log" ]]; then
      echo "===== ${log}" >&2
      cat "$log" >&2
    fi
  done
}

read -r ZLINK_CPP_E2E_RESERVED_PORT DELIVERYDISPATCH_API_HTTP_PORT DELIVERYDISPATCH_CENTER_ROUTE DELIVERYDISPATCH_COURIER_ROUTE DELIVERYDISPATCH_TRACKING_ROUTE DELIVERYDISPATCH_TRACKING_SPOT_ROUTER DELIVERYDISPATCH_TRACKING_SPOT DELIVERYDISPATCH_STATUS_FANOUT DELIVERYDISPATCH_CUSTOMER_STREAM DELIVERYDISPATCH_CUSTOMER_SPOT_ROUTER DELIVERYDISPATCH_CUSTOMER_SPOT DELIVERYDISPATCH_COURIER_STREAM DELIVERYDISPATCH_COURIER_SESSION_SPOT_ROUTER DELIVERYDISPATCH_COURIER_SESSION_SPOT DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTE DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTER DELIVERYDISPATCH_COURIER_ACTOR_NODE1 DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTE DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTER DELIVERYDISPATCH_COURIER_ACTOR_NODE2 <<<"$(python3 - <<'PY'
import socket
sockets = []
for _ in range(20):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    sockets.append(sock)
ports = [sock.getsockname()[1] for sock in sockets]
print(
    f"{ports[0]} "
    f"{ports[1]} "
    f"tcp://127.0.0.1:{ports[2]} "
    f"tcp://127.0.0.1:{ports[3]} "
    f"tcp://127.0.0.1:{ports[4]} "
    f"tcp://127.0.0.1:{ports[5]} "
    f"tcp://127.0.0.1:{ports[6]} "
    f"tcp://127.0.0.1:{ports[7]} "
    f"tcp://127.0.0.1:{ports[8]} "
    f"tcp://127.0.0.1:{ports[9]} "
    f"tcp://127.0.0.1:{ports[10]} "
    f"tcp://127.0.0.1:{ports[11]} "
    f"tcp://127.0.0.1:{ports[12]} "
    f"tcp://127.0.0.1:{ports[13]} "
    f"tcp://127.0.0.1:{ports[14]} "
    f"tcp://127.0.0.1:{ports[15]} "
    f"tcp://127.0.0.1:{ports[16]} "
    f"tcp://127.0.0.1:{ports[17]} "
    f"tcp://127.0.0.1:{ports[18]} "
    f"tcp://127.0.0.1:{ports[19]}"
)
for sock in sockets:
    sock.close()
PY
)"
if [[ -n "${ZLINK_CPP_E2E_OWNED_REDIS_CONTAINER:-}" && -n "${ZLINK_CPP_E2E_REDIS_ENDPOINT:-}" ]]; then
  REDIS_CONTAINER_NAME="$ZLINK_CPP_E2E_OWNED_REDIS_CONTAINER"
  echo "redis endpoint=$ZLINK_CPP_E2E_REDIS_ENDPOINT (existing owned container $REDIS_CONTAINER_NAME)"
elif [[ -n "${ZLINK_CPP_E2E_REDIS_ENDPOINT:-}" ]]; then
  echo "External Redis endpoint is not supported by the C++ DeliveryDispatch e2e runner." >&2
  exit 2
else
  zlink_redis_start_scoped_assign REDIS_CONTAINER_NAME redis_port \
    "zlink-redis-cpp-e2e-deliverydispatch" "redis:7-alpine"
  REDIS_CONTAINER_OWNED=1
  export ZLINK_CPP_E2E_REDIS_ENDPOINT="tcp://127.0.0.1:${redis_port}"
fi
export ZLINK_CPP_E2E_REDIS_KEY_PREFIX="${ZLINK_CPP_E2E_REDIS_KEY_PREFIX:-deliverydispatch:$$:}"
export DELIVERYDISPATCH_API_HTTP="http://127.0.0.1:${DELIVERYDISPATCH_API_HTTP_PORT}"
export DELIVERYDISPATCH_CENTER_ROUTE
export DELIVERYDISPATCH_COURIER_ROUTE
export DELIVERYDISPATCH_TRACKING_ROUTE
export DELIVERYDISPATCH_TRACKING_SPOT_ROUTER
export DELIVERYDISPATCH_TRACKING_SPOT
export DELIVERYDISPATCH_STATUS_FANOUT
export DELIVERYDISPATCH_CUSTOMER_STREAM
export DELIVERYDISPATCH_CUSTOMER_SPOT_ROUTER
export DELIVERYDISPATCH_CUSTOMER_SPOT
export DELIVERYDISPATCH_COURIER_STREAM
export DELIVERYDISPATCH_SESSION_STREAM="$DELIVERYDISPATCH_CUSTOMER_STREAM"
export DELIVERYDISPATCH_COURIER_SESSION_SPOT_ROUTER
export DELIVERYDISPATCH_COURIER_SESSION_SPOT
export DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTE
export DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTER
export DELIVERYDISPATCH_COURIER_ACTOR_NODE1
export DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTE
export DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTER
export DELIVERYDISPATCH_COURIER_ACTOR_NODE2

port_of() {
  local endpoint="$1"
  echo "${endpoint##*:}"
}

wait_port() {
  local label="$1"
  local port="$2"
  for _ in $(seq 1 "$LOCAL_READINESS_ATTEMPTS"); do
    if (echo >"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep "$LOCAL_READINESS_POLL_SECONDS"
  done
  echo "timed out waiting ${LOCAL_READINESS_TIMEOUT_SECONDS}s for ${label} on ${port}" >&2
  dump_logs
  return 1
}

wait_port redis "$(port_of "$ZLINK_CPP_E2E_REDIS_ENDPOINT")"

wait_http_health() {
  local label="$1"
  local url="$2"
  for _ in $(seq 1 "$LOCAL_READINESS_ATTEMPTS"); do
    if python3 - "$url" "$HTTP_PROBE_TIMEOUT_SECONDS" <<'PY' >/dev/null 2>&1
import sys
import urllib.request

url = sys.argv[1]
timeout = float(sys.argv[2])
with urllib.request.urlopen(f"{url}/health", timeout=timeout) as response:
    if response.status != 200:
        raise SystemExit(1)
PY
    then
      return 0
    fi
    sleep "$LOCAL_READINESS_POLL_SECONDS"
  done
  echo "timed out waiting ${LOCAL_READINESS_TIMEOUT_SECONDS}s for ${label} health" >&2
  dump_logs
  return 1
}

wait_framework_probe() {
  local log="$LOG_DIR/probe.log"
  for _ in $(seq 1 "$LOCAL_READINESS_ATTEMPTS"); do
    if "$BIN_DIR/zlink_cpp_e2e_delivery_dispatch_probe" >"$log" 2>&1; then
      return 0
    fi
    sleep "$LOCAL_READINESS_POLL_SECONDS"
  done
  echo "timed out waiting ${LOCAL_READINESS_TIMEOUT_SECONDS}s for DeliveryDispatch framework probe" >&2
  dump_logs
  return 1
}

start_role() {
  local name="$1"
  shift
  "$@" >"$LOG_DIR/${name}.log" 2>&1 &
  PIDS+=("$!")
}

cmake --build "$BUILD_DIR" --target \
  zlink_cpp_e2e_delivery_dispatch_dispatch_api \
  zlink_cpp_e2e_delivery_dispatch_dispatch_center \
  zlink_cpp_e2e_delivery_dispatch_courier_gateway \
  zlink_cpp_e2e_delivery_dispatch_courier_actor_node \
  zlink_cpp_e2e_delivery_dispatch_customer_gateway \
  zlink_cpp_e2e_delivery_dispatch_courier_session \
  zlink_cpp_e2e_delivery_dispatch_tracking \
  zlink_cpp_e2e_delivery_dispatch_probe \
  zlink_cpp_e2e_delivery_dispatch_client >/dev/null

start_role tracking "$BIN_DIR/zlink_cpp_e2e_delivery_dispatch_tracking"
start_role customer-gateway "$BIN_DIR/zlink_cpp_e2e_delivery_dispatch_customer_gateway"
start_role courier-session "$BIN_DIR/zlink_cpp_e2e_delivery_dispatch_courier_session"
start_role courier-actor-node-1 "$BIN_DIR/zlink_cpp_e2e_delivery_dispatch_courier_actor_node" delivery-courier-node-1
start_role courier-actor-node-2 "$BIN_DIR/zlink_cpp_e2e_delivery_dispatch_courier_actor_node" delivery-courier-node-2
start_role courier-gateway "$BIN_DIR/zlink_cpp_e2e_delivery_dispatch_courier_gateway"
start_role dispatch-center "$BIN_DIR/zlink_cpp_e2e_delivery_dispatch_dispatch_center"
start_role dispatch-api "$BIN_DIR/zlink_cpp_e2e_delivery_dispatch_dispatch_api"

wait_http_health dispatch-api "$DELIVERYDISPATCH_API_HTTP"
wait_framework_probe

"$BIN_DIR/zlink_cpp_e2e_delivery_dispatch_client" \
  --api-url "$DELIVERYDISPATCH_API_HTTP" \
  --stream-endpoint "$DELIVERYDISPATCH_CUSTOMER_STREAM" \
  --courier-stream-endpoint "$DELIVERYDISPATCH_COURIER_STREAM" >"$LOG_DIR/client.log" 2>&1 || {
  dump_logs
  exit 1
}

require_log_marker() {
  local pattern="$1"
  local file="$2"
  if ! grep -q "$pattern" "$file"; then
    echo "missing marker '$pattern' in $file" >&2
    dump_logs
    exit 1
  fi
}

require_log_marker "deliverydispatch-server-evidence=completed" "$LOG_DIR/client.log"
require_log_marker "deliverydispatch-reassignment=completed" "$LOG_DIR/client.log"
if ! grep -Rq "message flow" "$DELIVERYDISPATCH_LOG_DIR"; then
  echo "missing any message flow evidence under $DELIVERYDISPATCH_LOG_DIR" >&2
  dump_logs
  exit 1
fi
require_log_marker "message flow" "$DELIVERYDISPATCH_LOG_DIR/flow-courier-gateway.log"
require_log_marker "message flow" "$DELIVERYDISPATCH_LOG_DIR/flow-delivery-courier-node-1.log"
require_log_marker "message flow" "$DELIVERYDISPATCH_LOG_DIR/flow-delivery-courier-node-2.log"
require_log_marker "message flow" "$DELIVERYDISPATCH_LOG_DIR/flow-customer-gateway.log"
require_log_marker "message flow" "$DELIVERYDISPATCH_LOG_DIR/flow-courier-session.log"
echo "delivery-dispatch e2e result=passed"
