#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
export DELIVERYDISPATCH_LOG_DIR="${DELIVERYDISPATCH_LOG_DIR:-${SCRIPT_DIR}/logs}"
mkdir -p "$DELIVERYDISPATCH_LOG_DIR"
rm -f "$DELIVERYDISPATCH_LOG_DIR"/*.log
BUILD_DIR="${ZLINK_CPP_BUILD_DIR:-$CPP_ROOT/build}"
BIN_DIR="$BUILD_DIR"
if [[ ! -x "$BIN_DIR/sample_cpp_framework_deliverydispatch_client" && -x "$BIN_DIR/linux-ninja-debug/sample_cpp_framework_deliverydispatch_client" ]]; then
  BIN_DIR="$BIN_DIR/linux-ninja-debug"
fi

PIDS=()
RUN_DIR="$(mktemp -d)"
LOG_DIR="$RUN_DIR/logs"
mkdir -p "$LOG_DIR"
cleanup() {
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
        kill -9 "${pid}" >/dev/null 2>&1 || true
      fi
    fi
    wait "${pid}" 2>/dev/null || true
  done
}
trap cleanup EXIT

read -r DELIVERYDISPATCH_REGISTRY_PUB DELIVERYDISPATCH_REGISTRY DELIVERYDISPATCH_API_HTTP_PORT DELIVERYDISPATCH_CENTER_ROUTE DELIVERYDISPATCH_COURIER_ROUTE DELIVERYDISPATCH_TRACKING_ROUTE DELIVERYDISPATCH_STATUS_FANOUT DELIVERYDISPATCH_CUSTOMER_STREAM DELIVERYDISPATCH_CUSTOMER_SPOT_ROUTER DELIVERYDISPATCH_CUSTOMER_SPOT DELIVERYDISPATCH_COURIER_STREAM DELIVERYDISPATCH_COURIER_SESSION_SPOT_ROUTER DELIVERYDISPATCH_COURIER_SESSION_SPOT DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTE DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTER DELIVERYDISPATCH_COURIER_ACTOR_NODE1 DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTE DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTER DELIVERYDISPATCH_COURIER_ACTOR_NODE2 <<<"$(python3 - <<'PY'
import socket
sockets = []
for _ in range(19):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    sockets.append(sock)
ports = [sock.getsockname()[1] for sock in sockets]
print(
    f"tcp://127.0.0.1:{ports[0]} "
    f"tcp://127.0.0.1:{ports[1]} "
    f"{ports[2]} "
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
    f"tcp://127.0.0.1:{ports[18]}"
)
for sock in sockets:
    sock.close()
PY
)"
export DELIVERYDISPATCH_REGISTRY_PUB
export DELIVERYDISPATCH_REGISTRY
export DELIVERYDISPATCH_API_HTTP="http://127.0.0.1:${DELIVERYDISPATCH_API_HTTP_PORT}"
export DELIVERYDISPATCH_CENTER_ROUTE
export DELIVERYDISPATCH_COURIER_ROUTE
export DELIVERYDISPATCH_TRACKING_ROUTE
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
  for _ in $(seq 1 150); do
    if (echo >"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "timed out waiting for ${label} on ${port}" >&2
  return 1
}

start_role() {
  local name="$1"
  shift
  "$@" >"$LOG_DIR/${name}.log" 2>&1 &
  PIDS+=("$!")
}

cmake --build "$BUILD_DIR" --target \
  sample_cpp_framework_deliverydispatch_registry \
  sample_cpp_framework_deliverydispatch_dispatch_api \
  sample_cpp_framework_deliverydispatch_dispatch_center \
  sample_cpp_framework_deliverydispatch_courier_gateway \
  sample_cpp_framework_deliverydispatch_courier_actor_node \
  sample_cpp_framework_deliverydispatch_customer_gateway \
  sample_cpp_framework_deliverydispatch_courier_session \
  sample_cpp_framework_deliverydispatch_tracking \
  sample_cpp_framework_deliverydispatch_probe \
  sample_cpp_framework_deliverydispatch_client >/dev/null

start_role registry "$BIN_DIR/sample_cpp_framework_deliverydispatch_registry"
wait_port registry "$(port_of "$DELIVERYDISPATCH_REGISTRY")"

start_role tracking "$BIN_DIR/sample_cpp_framework_deliverydispatch_tracking"
wait_port tracking "$(port_of "$DELIVERYDISPATCH_TRACKING_ROUTE")"

start_role customer-gateway "$BIN_DIR/sample_cpp_framework_deliverydispatch_customer_gateway"
wait_port customer-stream "$(port_of "$DELIVERYDISPATCH_CUSTOMER_STREAM")"
wait_port customer-spot "$(port_of "$DELIVERYDISPATCH_CUSTOMER_SPOT_ROUTER")"

start_role courier-session "$BIN_DIR/sample_cpp_framework_deliverydispatch_courier_session"
wait_port courier-stream "$(port_of "$DELIVERYDISPATCH_COURIER_STREAM")"
wait_port courier-session-spot "$(port_of "$DELIVERYDISPATCH_COURIER_SESSION_SPOT_ROUTER")"

start_role courier-actor-node-1 "$BIN_DIR/sample_cpp_framework_deliverydispatch_courier_actor_node" delivery-courier-node-1
wait_port courier-actor-node-1 "$(port_of "$DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTE")"
wait_port courier-actor-node-1-spot "$(port_of "$DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTER")"

start_role courier-actor-node-2 "$BIN_DIR/sample_cpp_framework_deliverydispatch_courier_actor_node" delivery-courier-node-2
wait_port courier-actor-node-2 "$(port_of "$DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTE")"
wait_port courier-actor-node-2-spot "$(port_of "$DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTER")"

start_role courier-gateway "$BIN_DIR/sample_cpp_framework_deliverydispatch_courier_gateway"
wait_port courier-gateway "$(port_of "$DELIVERYDISPATCH_COURIER_ROUTE")"

start_role dispatch-center "$BIN_DIR/sample_cpp_framework_deliverydispatch_dispatch_center"
wait_port dispatch-center "$(port_of "$DELIVERYDISPATCH_CENTER_ROUTE")"

start_role dispatch-api "$BIN_DIR/sample_cpp_framework_deliverydispatch_dispatch_api"
wait_port dispatch-api "$DELIVERYDISPATCH_API_HTTP_PORT"

"$BIN_DIR/sample_cpp_framework_deliverydispatch_probe" >"$LOG_DIR/probe.log" 2>&1 || {
  cat "$LOG_DIR/probe.log" >&2
  exit 1
}
grep -q "topology=ready" "$LOG_DIR/probe.log"

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
grep -q "message flow" "$DELIVERYDISPATCH_LOG_DIR/flow-courier-gateway.log"
grep -q "message flow" "$DELIVERYDISPATCH_LOG_DIR/flow-delivery-courier-node-1.log"
grep -q "message flow" "$DELIVERYDISPATCH_LOG_DIR/flow-delivery-courier-node-2.log"
grep -q "message flow" "$DELIVERYDISPATCH_LOG_DIR/flow-customer-gateway.log"
grep -q "message flow" "$DELIVERYDISPATCH_LOG_DIR/flow-courier-session.log"
echo "deliverydispatch sample result=passed"
