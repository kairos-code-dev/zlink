#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../redis-common.sh"
CPP_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
export DELIVERYDISPATCH_LOG_DIR="${DELIVERYDISPATCH_LOG_DIR:-${SCRIPT_DIR}/logs}"
mkdir -p "$DELIVERYDISPATCH_LOG_DIR"
rm -f "$DELIVERYDISPATCH_LOG_DIR"/*.log
BUILD_DIR="${ZLINK_CPP_BUILD_DIR:-$CPP_ROOT/build}"
BIN_DIR="$BUILD_DIR"
LOCAL_READINESS_TIMEOUT_SECONDS=30
LOCAL_READINESS_POLL_SECONDS=0.5
LOCAL_READINESS_ATTEMPTS="$(
  python3 - "$LOCAL_READINESS_TIMEOUT_SECONDS" "$LOCAL_READINESS_POLL_SECONDS" <<'PY'
import math
import sys

timeout = float(sys.argv[1])
poll = float(sys.argv[2])
print(max(1, math.ceil(timeout / poll)))
PY
)"
cmake -S "$CPP_ROOT" -B "$BUILD_DIR" -DZLINK_FRAMEWORK_CPP_BUILD_SAMPLES=ON >/dev/null
if [[ ! -x "$BIN_DIR/sample_cpp_framework_deliverydispatch_client" && -x "$BIN_DIR/linux-ninja-debug/sample_cpp_framework_deliverydispatch_client" ]]; then
  BIN_DIR="$BIN_DIR/linux-ninja-debug"
fi

PIDS=()
RUN_DIR="${DELIVERYDISPATCH_RUN_DIR:-$(mktemp -d)}"
LOG_DIR="$RUN_DIR/logs"
REDIS_CONTAINER_NAME=""
mkdir -p "$LOG_DIR"
cleanup() {
  local code=$?
  local cleanup_failed=0
  local status
  for pid in "${PIDS[@]}"; do
    if kill -0 "${pid}" >/dev/null 2>&1; then
      kill "${pid}" >/dev/null 2>&1 || true
      for _ in $(seq 1 40); do
        if ! kill -0 "${pid}" >/dev/null 2>&1; then
          break
        fi
        sleep 0.05
      done
      if kill -0 "${pid}" >/dev/null 2>&1; then
        echo "forced cleanup process ${pid}" >&2
        kill -9 "${pid}" >/dev/null 2>&1 || true
        cleanup_failed=1
      fi
    fi
    set +e
    wait "${pid}" 2>/dev/null
    status=$?
    set -e
    if [[ "$status" != "0" && "$status" != "127" && "$status" != "130" && "$status" != "143" ]]; then
      echo "cleanup process ${pid} exited unexpectedly with status ${status}" >&2
      cleanup_failed=1
    fi
  done
  if [[ -n "$REDIS_CONTAINER_NAME" ]]; then
    docker rm -fv "$REDIS_CONTAINER_NAME" >/dev/null 2>&1 || true
  fi
  if [[ "${DELIVERYDISPATCH_KEEP_RUN_DIR:-}" == "1" ]]; then
    echo "runDir=$RUN_DIR"
  else
    [[ -z "${DELIVERYDISPATCH_RUN_DIR:-}" ]] && rm -rf "$RUN_DIR"
  fi
  if [[ "$cleanup_failed" -ne 0 && "$code" -eq 0 ]]; then
    code=1
  fi
  return "$code"
}
trap 'cleanup; status=$?; exit "$status"' EXIT

read -r DELIVERYDISPATCH_RESERVED_PORT DELIVERYDISPATCH_API_HTTP_PORT DELIVERYDISPATCH_DISPATCH_ROUTE DELIVERYDISPATCH_DISPATCH_SPOT_ROUTER DELIVERYDISPATCH_TRACKING_ROUTE DELIVERYDISPATCH_TRACKING_SPOT_ROUTER DELIVERYDISPATCH_TRACKING_SPOT DELIVERYDISPATCH_DISPATCH_SPOT DELIVERYDISPATCH_CUSTOMER_STREAM DELIVERYDISPATCH_CUSTOMER_SPOT_ROUTER DELIVERYDISPATCH_CUSTOMER_SPOT DELIVERYDISPATCH_COURIER_STREAM DELIVERYDISPATCH_COURIER_SESSION_SPOT_ROUTER DELIVERYDISPATCH_COURIER_SESSION_SPOT DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTE DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTER DELIVERYDISPATCH_COURIER_ACTOR_NODE1 DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTE DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTER DELIVERYDISPATCH_COURIER_ACTOR_NODE2 <<<"$(python3 - <<'PY'
import socket
sockets = []
chosen = set()
while len(sockets) < 20:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    if port in chosen or port + 1000 in chosen:
        sock.close()
        continue
    chosen.add(port)
    chosen.add(port + 1000)
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
if [[ -z "$DELIVERYDISPATCH_RESERVED_PORT" || -z "$DELIVERYDISPATCH_COURIER_ACTOR_NODE2" ]]; then
  echo "Failed to allocate local TCP ports for the DeliveryDispatch sample." >&2
  echo "This environment may block local socket creation." >&2
  exit 1
fi
zlink_redis_start_scoped_assign REDIS_CONTAINER_NAME redis_port \
  "zlink-redis-cpp-sample-deliverydispatch" "redis:7-alpine"
export DELIVERYDISPATCH_REDIS_ENDPOINT="tcp://127.0.0.1:${redis_port}"
export DELIVERYDISPATCH_REDIS_KEY_PREFIX="${DELIVERYDISPATCH_REDIS_KEY_PREFIX:-deliverydispatch:$$:}"
export DELIVERYDISPATCH_API_HTTP="http://127.0.0.1:${DELIVERYDISPATCH_API_HTTP_PORT}"
export DELIVERYDISPATCH_DISPATCH_ROUTE
export DELIVERYDISPATCH_DISPATCH_SPOT_ROUTER
export DELIVERYDISPATCH_TRACKING_ROUTE
export DELIVERYDISPATCH_TRACKING_SPOT_ROUTER
export DELIVERYDISPATCH_TRACKING_SPOT
export DELIVERYDISPATCH_DISPATCH_SPOT
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
export ZLINK_CPP_AUTO_CONNECT_TRACE="${ZLINK_CPP_AUTO_CONNECT_TRACE-1}"

port_of() {
  local endpoint="$1"
  echo "${endpoint##*:}"
}

wait_port() {
  local label="$1"
  local port="$2"
  for _ in $(seq 1 150); do
    if (echo >"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "timed out waiting for ${label} on ${port}" >&2
  for log in "$LOG_DIR"/*.log; do
    if [[ -f "$log" ]]; then
      echo "===== ${log}" >&2
      cat "$log" >&2
    fi
  done
  return 1
}

wait_port redis "$(port_of "$DELIVERYDISPATCH_REDIS_ENDPOINT")"

wait_framework_probe() {
  for _ in $(seq 1 "$LOCAL_READINESS_ATTEMPTS"); do
    if "$BIN_DIR/sample_cpp_framework_deliverydispatch_probe" >"$LOG_DIR/probe.log" 2>&1; then
      grep -q "topology=ready" "$LOG_DIR/probe.log"
      return 0
    fi
    sleep "$LOCAL_READINESS_POLL_SECONDS"
  done
  echo "timed out waiting ${LOCAL_READINESS_TIMEOUT_SECONDS}s for DeliveryDispatch sample probe" >&2
  dump_logs
  return 1
}

start_role() {
  local name="$1"
  shift
  stdbuf -oL -eL "$@" >"$LOG_DIR/${name}.log" 2>&1 &
  PIDS+=("$!")
}

dump_logs() {
  for log in "$LOG_DIR"/*.log; do
    if [[ -f "$log" ]]; then
      echo "===== ${log}" >&2
      cat "$log" >&2
    fi
  done
  for log in "$DELIVERYDISPATCH_LOG_DIR"/flow-*.log; do
    if [[ -f "$log" ]]; then
      echo "===== ${log}" >&2
      cat "$log" >&2
    fi
  done
}

cmake --build "$BUILD_DIR" --target \
  sample_cpp_framework_deliverydispatch_dispatch \
  sample_cpp_framework_deliverydispatch_courier_actor_node \
  sample_cpp_framework_deliverydispatch_customer_gateway \
  sample_cpp_framework_deliverydispatch_courier_session \
  sample_cpp_framework_deliverydispatch_tracking \
  sample_cpp_framework_deliverydispatch_probe \
  sample_cpp_framework_deliverydispatch_client >/dev/null

start_role tracking "$BIN_DIR/sample_cpp_framework_deliverydispatch_tracking"
start_role customer-gateway "$BIN_DIR/sample_cpp_framework_deliverydispatch_customer_gateway"
start_role courier-session "$BIN_DIR/sample_cpp_framework_deliverydispatch_courier_session"
start_role courier-actor-node-1 "$BIN_DIR/sample_cpp_framework_deliverydispatch_courier_actor_node" delivery-courier-node-1
start_role courier-actor-node-2 "$BIN_DIR/sample_cpp_framework_deliverydispatch_courier_actor_node" delivery-courier-node-2
start_role dispatch "$BIN_DIR/sample_cpp_framework_deliverydispatch_dispatch"

wait_port tracking "$(port_of "$DELIVERYDISPATCH_TRACKING_ROUTE")"
wait_port tracking-spot "$(port_of "$DELIVERYDISPATCH_TRACKING_SPOT_ROUTER")"
wait_port customer-stream "$(port_of "$DELIVERYDISPATCH_CUSTOMER_STREAM")"
wait_port customer-spot "$(port_of "$DELIVERYDISPATCH_CUSTOMER_SPOT_ROUTER")"
wait_port courier-stream "$(port_of "$DELIVERYDISPATCH_COURIER_STREAM")"
wait_port courier-session-spot "$(port_of "$DELIVERYDISPATCH_COURIER_SESSION_SPOT_ROUTER")"
wait_port courier-actor-node-1-spot "$(port_of "$DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTER")"
wait_port courier-actor-node-2-spot "$(port_of "$DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTER")"
wait_port dispatch "$(port_of "$DELIVERYDISPATCH_DISPATCH_ROUTE")"
wait_port dispatch-http "$DELIVERYDISPATCH_API_HTTP_PORT"
wait_framework_probe

"$BIN_DIR/sample_cpp_framework_deliverydispatch_client" \
  --api-url "$DELIVERYDISPATCH_API_HTTP" \
  --stream-endpoint "$DELIVERYDISPATCH_CUSTOMER_STREAM" \
  --courier-stream-endpoint "$DELIVERYDISPATCH_COURIER_STREAM" >"$LOG_DIR/client.log" 2>&1 || {
  cat "$LOG_DIR/client.log" >&2
  for log in "$LOG_DIR"/*.log; do
    echo "===== ${log}" >&2
    cat "$log" >&2
  done
  exit 1
}

grep -q "deliverydispatch-server-evidence=completed" "$LOG_DIR/client.log"
grep -q "deliverydispatch-reassignment=completed" "$LOG_DIR/client.log"
grep -q "deliverydispatch=completed" "$LOG_DIR/client.log"
grep -Rq "message flow" "$DELIVERYDISPATCH_LOG_DIR"
grep -q "message flow" "$DELIVERYDISPATCH_LOG_DIR/flow-dispatch.log"
grep -q "message flow" "$DELIVERYDISPATCH_LOG_DIR/flow-delivery-courier-node-1.log"
grep -q "message flow" "$DELIVERYDISPATCH_LOG_DIR/flow-delivery-courier-node-2.log"
grep -q "message flow" "$DELIVERYDISPATCH_LOG_DIR/flow-customer-gateway.log"
grep -q "message flow" "$DELIVERYDISPATCH_LOG_DIR/flow-courier-session.log"
grep -q "zlink auto-connect publish .* type=spot mesh=delivery-couriers" "$LOG_DIR/courier-actor-node-1.log"
grep -q "zlink auto-connect scan type=spot mesh=delivery-couriers" "$LOG_DIR/courier-session.log"
grep -Rq "zlink auto-connect dial type=spot mesh=delivery-couriers" "$LOG_DIR"
echo "deliverydispatch sample result=passed"
