#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
export SHOPPINGMALL_LOG_DIR="${SHOPPINGMALL_LOG_DIR:-${SCRIPT_DIR}/logs}"
mkdir -p "$SHOPPINGMALL_LOG_DIR"
rm -f "$SHOPPINGMALL_LOG_DIR"/*.log
BUILD_DIR="${ZLINK_CPP_BUILD_DIR:-$CPP_ROOT/build}"
BIN_DIR="$BUILD_DIR"
if [[ ! -x "$BIN_DIR/sample_cpp_framework_shoppingmall_client" && -x "$BIN_DIR/linux-ninja-debug/sample_cpp_framework_shoppingmall_client" ]]; then
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

read -r SHOPPINGMALL_REGISTRY_PUB_ENDPOINT SHOPPINGMALL_REGISTRY_ROUTER_ENDPOINT SHOPPINGMALL_API_A_HTTP_PORT SHOPPINGMALL_API_B_HTTP_PORT SHOPPINGMALL_API_A_ROUTE_ENDPOINT SHOPPINGMALL_API_B_ROUTE_ENDPOINT SHOPPINGMALL_WORKFLOW_A_HTTP_PORT SHOPPINGMALL_WORKFLOW_B_HTTP_PORT SHOPPINGMALL_WORKFLOW_A_ROUTE_ENDPOINT SHOPPINGMALL_WORKFLOW_B_ROUTE_ENDPOINT <<<"$(python3 - <<'PY'
import socket
sockets = []
for _ in range(10):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    sockets.append(sock)
ports = [sock.getsockname()[1] for sock in sockets]
print(
    f"tcp://127.0.0.1:{ports[0]} "
    f"tcp://127.0.0.1:{ports[1]} "
    f"{ports[2]} {ports[3]} "
    f"tcp://127.0.0.1:{ports[4]} "
    f"tcp://127.0.0.1:{ports[5]} "
    f"{ports[6]} {ports[7]} "
    f"tcp://127.0.0.1:{ports[8]} "
    f"tcp://127.0.0.1:{ports[9]}"
)
for sock in sockets:
    sock.close()
PY
)"
export SHOPPINGMALL_REGISTRY_PUB_ENDPOINT
export SHOPPINGMALL_REGISTRY_ROUTER_ENDPOINT
export SHOPPINGMALL_API_A_ROUTE_ENDPOINT
export SHOPPINGMALL_API_B_ROUTE_ENDPOINT
export SHOPPINGMALL_API_A_HTTP_URL="http://127.0.0.1:${SHOPPINGMALL_API_A_HTTP_PORT}"
export SHOPPINGMALL_API_B_HTTP_URL="http://127.0.0.1:${SHOPPINGMALL_API_B_HTTP_PORT}"
export SHOPPINGMALL_WORKFLOW_A_HTTP_URL="http://127.0.0.1:${SHOPPINGMALL_WORKFLOW_A_HTTP_PORT}"
export SHOPPINGMALL_WORKFLOW_B_HTTP_URL="http://127.0.0.1:${SHOPPINGMALL_WORKFLOW_B_HTTP_PORT}"
export SHOPPINGMALL_WORKFLOW_A_ROUTE_ENDPOINT
export SHOPPINGMALL_WORKFLOW_B_ROUTE_ENDPOINT

port_of() {
  echo "${1##*:}"
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

start_role registry "$BIN_DIR/sample_cpp_framework_shoppingmall_registry"
wait_port registry "$(port_of "$SHOPPINGMALL_REGISTRY_ROUTER_ENDPOINT")"

start_role workflow-a env SHOPPINGMALL_WORKFLOW_INSTANCE=workflow-a "$BIN_DIR/sample_cpp_framework_shoppingmall_order_workflow"
wait_port workflow-a "$SHOPPINGMALL_WORKFLOW_A_HTTP_PORT"
start_role workflow-b env SHOPPINGMALL_WORKFLOW_INSTANCE=workflow-b "$BIN_DIR/sample_cpp_framework_shoppingmall_order_workflow"
wait_port workflow-b "$SHOPPINGMALL_WORKFLOW_B_HTTP_PORT"

start_role api-a env SHOPPINGMALL_INSTANCE=api-a "$BIN_DIR/sample_cpp_framework_shoppingmall_commerce_api"
wait_port api-a "$SHOPPINGMALL_API_A_HTTP_PORT"
start_role api-b env SHOPPINGMALL_INSTANCE=api-b "$BIN_DIR/sample_cpp_framework_shoppingmall_commerce_api"
wait_port api-b "$SHOPPINGMALL_API_B_HTTP_PORT"

"$BIN_DIR/sample_cpp_framework_shoppingmall_client" \
  --api-url "$SHOPPINGMALL_API_A_HTTP_URL" >"$LOG_DIR/client.log" 2>&1 || {
  cat "$LOG_DIR/client.log" >&2
  for log in "$LOG_DIR"/*.log; do
    echo "===== ${log}" >&2
    cat "$log" >&2
  done
  exit 1
}

grep -q "shoppingmall-server-evidence=completed" "$LOG_DIR/client.log"
grep -Rq "message flow" "$SHOPPINGMALL_LOG_DIR"
