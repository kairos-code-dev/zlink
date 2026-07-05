#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${ZLINK_CPP_BUILD_DIR:-$CPP_ROOT/build}"
BIN_DIR="$BUILD_DIR"
if [[ ! -x "$BIN_DIR/sample_cpp_framework_shoppingmall_client" && -x "$BIN_DIR/linux-ninja-debug/sample_cpp_framework_shoppingmall_client" ]]; then
  BIN_DIR="$BIN_DIR/linux-ninja-debug"
fi

RUN_DIR="${SHOPPINGMALL_RUN_DIR:-$(mktemp -d)}"
RUN_ID="$(basename "$RUN_DIR")-$$-${RANDOM}"
LOG_DIR="$RUN_DIR/logs"
export SHOPPINGMALL_LOG_DIR="${SHOPPINGMALL_LOG_DIR:-$SCRIPT_DIR/logs}"
export SHOPPINGMALL_STATE_FILE="$RUN_DIR/shoppingmall-state.json"
mkdir -p "$LOG_DIR" "$SHOPPINGMALL_LOG_DIR"
rm -f "$SHOPPINGMALL_LOG_DIR"/*.log

PIDS=()
REDIS_CONTAINER_NAME=""
cleanup() {
  for ((i=${#PIDS[@]}-1; i>=0; i--)); do
    local pid="${PIDS[$i]}"
    if kill -0 "$pid" >/dev/null 2>&1; then
      kill "$pid" >/dev/null 2>&1 || true
    fi
  done
  for pid in "${PIDS[@]}"; do
    wait "$pid" 2>/dev/null || true
  done
  if [[ -n "$REDIS_CONTAINER_NAME" ]]; then
    docker rm -f "$REDIS_CONTAINER_NAME" >/dev/null 2>&1 || true
  fi
  if [[ "${SHOPPINGMALL_KEEP_RUN_DIR:-}" == "1" ]]; then
    echo "runDir=$RUN_DIR"
  else
    [[ -z "${SHOPPINGMALL_RUN_DIR:-}" ]] && rm -rf "$RUN_DIR"
  fi
}
trap cleanup EXIT

PORT_ALLOCATION_OUTPUT="$(python3 - <<'PY'
import socket
sockets = []
try:
    for _ in range(13):
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.bind(("127.0.0.1", 0))
        sockets.append(sock)
    ports = [sock.getsockname()[1] for sock in sockets]
    print(
        f"{ports[0]} {ports[1]} {ports[2]} "
        f"tcp://127.0.0.1:{ports[3]} tcp://127.0.0.1:{ports[4]} "
        f"{ports[5]} {ports[6]} "
        f"tcp://127.0.0.1:{ports[7]} tcp://127.0.0.1:{ports[8]} "
        f"tcp://127.0.0.1:{ports[9]} tcp://127.0.0.1:{ports[10]} "
        f"tcp://127.0.0.1:{ports[11]} tcp://127.0.0.1:{ports[12]}"
    )
except OSError as error:
    print(f"SOCKETLESS {error.errno}:{error.strerror}")
finally:
    for sock in sockets:
        sock.close()
PY
)"
if [[ "$PORT_ALLOCATION_OUTPUT" == SOCKETLESS* ]]; then
  echo "ShoppingMall runner requires local TCP sockets; allocation failed: ${PORT_ALLOCATION_OUTPUT#SOCKETLESS }" >&2
  exit 1
fi
read -r SHOPPINGMALL_REDIS_PORT SHOPPINGMALL_API_A_PORT SHOPPINGMALL_API_B_PORT SHOPPINGMALL_API_A_ROUTE SHOPPINGMALL_API_B_ROUTE SHOPPINGMALL_WORKFLOW_A_PORT SHOPPINGMALL_WORKFLOW_B_PORT SHOPPINGMALL_WORKFLOW_A_ROUTE SHOPPINGMALL_WORKFLOW_B_ROUTE SHOPPINGMALL_WORKFLOW_A_SPOT SHOPPINGMALL_WORKFLOW_A_SPOT_ROUTER SHOPPINGMALL_WORKFLOW_B_SPOT SHOPPINGMALL_WORKFLOW_B_SPOT_ROUTER <<<"$PORT_ALLOCATION_OUTPUT"

if [[ -z "${SHOPPINGMALL_REDIS_ENDPOINT:-}" ]]; then
  REDIS_CONTAINER_NAME="zlink-cpp-shoppingmall-sample-redis-$$"
  docker run -d --rm --name "$REDIS_CONTAINER_NAME" -p "127.0.0.1:${SHOPPINGMALL_REDIS_PORT}:6379" redis:7-alpine >/dev/null
  export SHOPPINGMALL_REDIS_ENDPOINT="tcp://127.0.0.1:${SHOPPINGMALL_REDIS_PORT}"
fi
export SHOPPINGMALL_REDIS_KEY_PREFIX="${SHOPPINGMALL_REDIS_KEY_PREFIX:-shoppingmall:cpp:${RUN_ID}:}"
export SHOPPINGMALL_API_A_HTTP_URL="http://127.0.0.1:${SHOPPINGMALL_API_A_PORT}"
export SHOPPINGMALL_API_B_HTTP_URL="http://127.0.0.1:${SHOPPINGMALL_API_B_PORT}"
export SHOPPINGMALL_API_A_ROUTE_ENDPOINT="$SHOPPINGMALL_API_A_ROUTE"
export SHOPPINGMALL_API_B_ROUTE_ENDPOINT="$SHOPPINGMALL_API_B_ROUTE"
export SHOPPINGMALL_WORKFLOW_A_HTTP_URL="http://127.0.0.1:${SHOPPINGMALL_WORKFLOW_A_PORT}"
export SHOPPINGMALL_WORKFLOW_B_HTTP_URL="http://127.0.0.1:${SHOPPINGMALL_WORKFLOW_B_PORT}"
export SHOPPINGMALL_WORKFLOW_A_ROUTE_ENDPOINT="$SHOPPINGMALL_WORKFLOW_A_ROUTE"
export SHOPPINGMALL_WORKFLOW_B_ROUTE_ENDPOINT="$SHOPPINGMALL_WORKFLOW_B_ROUTE"
export SHOPPINGMALL_WORKFLOW_A_SPOT_ENDPOINT="$SHOPPINGMALL_WORKFLOW_A_SPOT"
export SHOPPINGMALL_WORKFLOW_A_SPOT_ROUTER_ENDPOINT="$SHOPPINGMALL_WORKFLOW_A_SPOT_ROUTER"
export SHOPPINGMALL_WORKFLOW_B_SPOT_ENDPOINT="$SHOPPINGMALL_WORKFLOW_B_SPOT"
export SHOPPINGMALL_WORKFLOW_B_SPOT_ROUTER_ENDPOINT="$SHOPPINGMALL_WORKFLOW_B_SPOT_ROUTER"

port_of() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#http://}"
  echo "${endpoint##*:}"
}

wait_port() {
  local label="$1"
  local endpoint="$2"
  local port
  port="$(port_of "$endpoint")"
  for _ in $(seq 1 150); do
    if (echo >"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "timed out waiting for ${label} at ${endpoint}" >&2
  for log in "$LOG_DIR"/*.log "$SHOPPINGMALL_LOG_DIR"/*.log; do
    [[ -f "$log" ]] && { echo "===== $log" >&2; cat "$log" >&2; }
  done
  return 1
}

wait_http() {
  local label="$1"
  local endpoint="$2"
  for _ in $(seq 1 150); do
    if curl -fsS "${endpoint}/health" >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "timed out waiting for ${label} at ${endpoint}" >&2
  return 1
}

start_role() {
  local name="$1"
  shift
  stdbuf -oL -eL "$@" >"$LOG_DIR/${name}.log" 2>&1 &
  PIDS+=("$!")
}

cmake --build "$BUILD_DIR" --target \
  sample_cpp_framework_shoppingmall_commerce_api \
  sample_cpp_framework_shoppingmall_order_workflow \
  sample_cpp_framework_shoppingmall_client >/dev/null

wait_port redis "$SHOPPINGMALL_REDIS_ENDPOINT"

start_role workflow-a "$BIN_DIR/sample_cpp_framework_shoppingmall_order_workflow" --instance workflow-a
wait_port workflow-a-route "$SHOPPINGMALL_WORKFLOW_A_ROUTE_ENDPOINT"
wait_port workflow-a-spot "$SHOPPINGMALL_WORKFLOW_A_SPOT_ENDPOINT"
wait_port workflow-a-spot-router "$SHOPPINGMALL_WORKFLOW_A_SPOT_ROUTER_ENDPOINT"
wait_http workflow-a "$SHOPPINGMALL_WORKFLOW_A_HTTP_URL"

start_role workflow-b "$BIN_DIR/sample_cpp_framework_shoppingmall_order_workflow" --instance workflow-b
wait_port workflow-b-route "$SHOPPINGMALL_WORKFLOW_B_ROUTE_ENDPOINT"
wait_port workflow-b-spot "$SHOPPINGMALL_WORKFLOW_B_SPOT_ENDPOINT"
wait_port workflow-b-spot-router "$SHOPPINGMALL_WORKFLOW_B_SPOT_ROUTER_ENDPOINT"
wait_http workflow-b "$SHOPPINGMALL_WORKFLOW_B_HTTP_URL"

start_role api-a "$BIN_DIR/sample_cpp_framework_shoppingmall_commerce_api" --instance api-a
wait_port api-a-route "$SHOPPINGMALL_API_A_ROUTE_ENDPOINT"
wait_http api-a "$SHOPPINGMALL_API_A_HTTP_URL"

start_role api-b "$BIN_DIR/sample_cpp_framework_shoppingmall_commerce_api" --instance api-b
wait_port api-b-route "$SHOPPINGMALL_API_B_ROUTE_ENDPOINT"
wait_http api-b "$SHOPPINGMALL_API_B_HTTP_URL"

"$BIN_DIR/sample_cpp_framework_shoppingmall_client" >"$LOG_DIR/client.log" 2>&1 || {
  for log in "$LOG_DIR"/*.log "$SHOPPINGMALL_LOG_DIR"/*.log; do
    [[ -f "$log" ]] && { echo "===== $log" >&2; cat "$log" >&2; }
  done
  exit 1
}

grep -q "shoppingmall=completed" "$LOG_DIR/client.log"
grep -q "shoppingmall order: started" "$LOG_DIR/workflow-a.log"
grep -q "shoppingmall order: started" "$LOG_DIR/workflow-b.log"
grep -q "shoppingmall evidence:" "$LOG_DIR/api-a.log"
grep -Rq "message flow" "$SHOPPINGMALL_LOG_DIR"
grep -q "location-store-claim" "$SHOPPINGMALL_LOG_DIR/flow-api-a.log"
grep -q "location-store-claim" "$SHOPPINGMALL_LOG_DIR/flow-api-b.log"
grep -q "location-store-claim" "$SHOPPINGMALL_LOG_DIR/flow-workflow-a.log"
grep -q "location-store-claim" "$SHOPPINGMALL_LOG_DIR/flow-workflow-b.log"
echo "shoppingmall-server-evidence=completed"
echo "PASS ShoppingMall.Cpp"
