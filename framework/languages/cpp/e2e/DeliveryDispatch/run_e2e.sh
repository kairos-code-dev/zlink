#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
export DELIVERYDISPATCH_LOG_DIR="${DELIVERYDISPATCH_LOG_DIR:-${SCRIPT_DIR}/logs}"
mkdir -p "$DELIVERYDISPATCH_LOG_DIR"
rm -f "$DELIVERYDISPATCH_LOG_DIR"/*.log
BUILD_DIR="${ZLINK_CPP_BUILD_DIR:-$CPP_ROOT/build}"
BIN_DIR="$BUILD_DIR"
if [[ ! -x "$BIN_DIR/zlink_cpp_e2e_delivery_dispatch_client" && -x "$BIN_DIR/linux-ninja-debug/zlink_cpp_e2e_delivery_dispatch_client" ]]; then
  BIN_DIR="$BIN_DIR/linux-ninja-debug"
fi

PIDS=()
RUN_DIR="$(mktemp -d)"
LOG_DIR="$RUN_DIR/logs"
mkdir -p "$LOG_DIR"
cleanup() {
  for pid in "${PIDS[@]}"; do
    kill "${pid}" >/dev/null 2>&1 || true
    wait "${pid}" 2>/dev/null || true
  done
}
trap cleanup EXIT

read -r DELIVERYDISPATCH_REGISTRY_PUB DELIVERYDISPATCH_REGISTRY DELIVERYDISPATCH_API_HTTP_PORT DELIVERYDISPATCH_CENTER_ROUTE DELIVERYDISPATCH_COURIER_A_ROUTE DELIVERYDISPATCH_COURIER_B_ROUTE DELIVERYDISPATCH_TRACKING_ROUTE DELIVERYDISPATCH_STATUS_FANOUT DELIVERYDISPATCH_SESSION_STREAM <<<"$(python3 - <<'PY'
import socket
sockets = []
for _ in range(9):
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
    f"tcp://127.0.0.1:{ports[8]}"
)
for sock in sockets:
    sock.close()
PY
)"
export DELIVERYDISPATCH_REGISTRY_PUB
export DELIVERYDISPATCH_REGISTRY
export DELIVERYDISPATCH_API_HTTP="http://127.0.0.1:${DELIVERYDISPATCH_API_HTTP_PORT}"
export DELIVERYDISPATCH_CENTER_ROUTE
export DELIVERYDISPATCH_COURIER_A_ROUTE
export DELIVERYDISPATCH_COURIER_B_ROUTE
export DELIVERYDISPATCH_TRACKING_ROUTE
export DELIVERYDISPATCH_STATUS_FANOUT
export DELIVERYDISPATCH_SESSION_STREAM

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
  zlink_cpp_e2e_delivery_dispatch_registry \
  zlink_cpp_e2e_delivery_dispatch_dispatch_api \
  zlink_cpp_e2e_delivery_dispatch_dispatch_center \
  zlink_cpp_e2e_delivery_dispatch_courier \
  zlink_cpp_e2e_delivery_dispatch_tracking \
  zlink_cpp_e2e_delivery_dispatch_session \
  zlink_cpp_e2e_delivery_dispatch_probe \
  zlink_cpp_e2e_delivery_dispatch_client >/dev/null

start_role registry "$BIN_DIR/zlink_cpp_e2e_delivery_dispatch_registry"
wait_port registry "$(port_of "$DELIVERYDISPATCH_REGISTRY")"

start_role tracking "$BIN_DIR/zlink_cpp_e2e_delivery_dispatch_tracking"
wait_port tracking "$(port_of "$DELIVERYDISPATCH_TRACKING_ROUTE")"

start_role session "$BIN_DIR/zlink_cpp_e2e_delivery_dispatch_session"
wait_port session-stream "$(port_of "$DELIVERYDISPATCH_SESSION_STREAM")"

start_role courier-a env DELIVERYDISPATCH_COURIER_ID=courier-a \
  DELIVERYDISPATCH_COURIER_MODE=timeout-reassign \
  "$BIN_DIR/zlink_cpp_e2e_delivery_dispatch_courier"
wait_port courier-a "$(port_of "$DELIVERYDISPATCH_COURIER_A_ROUTE")"

start_role courier-b env DELIVERYDISPATCH_COURIER_ID=courier-b \
  DELIVERYDISPATCH_COURIER_MODE=accept \
  "$BIN_DIR/zlink_cpp_e2e_delivery_dispatch_courier"
wait_port courier-b "$(port_of "$DELIVERYDISPATCH_COURIER_B_ROUTE")"

start_role dispatch-center "$BIN_DIR/zlink_cpp_e2e_delivery_dispatch_dispatch_center"
wait_port dispatch-center "$(port_of "$DELIVERYDISPATCH_CENTER_ROUTE")"

start_role dispatch-api "$BIN_DIR/zlink_cpp_e2e_delivery_dispatch_dispatch_api"
wait_port dispatch-api "$DELIVERYDISPATCH_API_HTTP_PORT"

"$BIN_DIR/zlink_cpp_e2e_delivery_dispatch_probe" >"$LOG_DIR/probe.log" 2>&1 || {
  cat "$LOG_DIR/probe.log" >&2
  exit 1
}

"$BIN_DIR/zlink_cpp_e2e_delivery_dispatch_client" \
  --api-url "$DELIVERYDISPATCH_API_HTTP" \
  --stream-endpoint "$DELIVERYDISPATCH_SESSION_STREAM" >"$LOG_DIR/client.log" 2>&1 || {
  cat "$LOG_DIR/client.log" >&2
  for log in "$LOG_DIR"/*.log; do
    echo "===== ${log}" >&2
    cat "$log" >&2
  done
  exit 1
}

grep -q "deliverydispatch-server-evidence=completed" "$LOG_DIR/client.log"
grep -q "deliverydispatch-reassignment=completed" "$LOG_DIR/client.log"
grep -Rq "message flow" "$DELIVERYDISPATCH_LOG_DIR"
echo "delivery-dispatch e2e result=passed"
