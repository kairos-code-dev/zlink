#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${ZLINK_CPP_E2E_BUILD_DIR:-$CPP_DIR/build}"

read -r REGISTRY_PUB REGISTRY_ROUTER API_A API_B ROUTE_A ROUTE_B WORKFLOW_A HTTP_A HTTP_B CLIENT_ROUTE API_A2 ROUTE_A2 HTTP_A2 <<<"$(python3 - <<'PY'
import socket

sockets = []
ports = []
for _ in range(13):
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    sockets.append(s)
    ports.append(s.getsockname()[1])
print(" ".join(f"tcp://127.0.0.1:{p}" for p in ports[:7]), end=" ")
print(" ".join(f"http://127.0.0.1:{p}" for p in ports[7:9]), end=" ")
print(f"tcp://127.0.0.1:{ports[9]}", end=" ")
print(" ".join(f"tcp://127.0.0.1:{p}" for p in ports[10:12]), end=" ")
print(f"http://127.0.0.1:{ports[12]}")
for s in sockets:
    s.close()
PY
)"

RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$SCRIPT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"
echo "log_dir=$LOG_DIR"

cmake -S "$CPP_DIR" -B "$BUILD_DIR" >/dev/null
cmake --build "$BUILD_DIR" --target \
  zlink_cpp_e2e_registry_messaging_server \
  zlink_cpp_e2e_registry_messaging_client >/dev/null

SERVER="$BUILD_DIR/zlink_cpp_e2e_registry_messaging_server"
CLIENT="$BUILD_DIR/zlink_cpp_e2e_registry_messaging_client"
PIDS=()
LAST_PID=""

cleanup() {
  local code=$?
  for pid in "${PIDS[@]:-}"; do
    if kill -0 "$pid" >/dev/null 2>&1; then
      kill "$pid" >/dev/null 2>&1 || true
    fi
  done
  wait >/dev/null 2>&1 || true
  if [[ $code -ne 0 ]]; then
    echo "E2E failed. Logs: $LOG_DIR" >&2
  fi
  exit "$code"
}
trap cleanup EXIT

port_of() {
  local endpoint="$1"
  echo "${endpoint##*:}"
}

wait_port() {
  local name="$1"
  local endpoint="$2"
  local host="127.0.0.1"
  local port
  port="$(port_of "$endpoint")"
  for _ in $(seq 1 100); do
    if (echo >"/dev/tcp/${host}/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.05
  done
  echo "Timed out waiting for $name at $endpoint" >&2
  return 1
}

start_registry() {
  ZLINK_CPP_E2E_ROLE=registry \
  ZLINK_CPP_E2E_REGISTRY_PUB="$REGISTRY_PUB" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$SERVER" >"$LOG_DIR/registry.stdout.log" 2>"$LOG_DIR/registry.stderr.log" &
  PIDS+=("$!")
  wait_port registry-router "$REGISTRY_ROUTER"
}

start_provider() {
  local rid="$1"
  local api="$2"
  local route="$3"
  local http="$4"
  local instance="${5:-$rid}"
  if (($# >= 5)); then
    shift 5
  else
    shift 4
  fi
  env "$@" \
    ZLINK_CPP_E2E_ROLE=provider \
    ZLINK_CPP_E2E_PROVIDER_RID="$rid" \
    ZLINK_CPP_E2E_PROVIDER_INSTANCE="$instance" \
    ZLINK_CPP_E2E_API_ENDPOINT="$api" \
    ZLINK_CPP_E2E_ROUTE_ENDPOINT="$route" \
    ZLINK_CPP_E2E_HTTP_ENDPOINT="$http" \
    ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
    ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$SERVER" >"$LOG_DIR/$rid.stdout.log" 2>"$LOG_DIR/$rid.stderr.log" &
  LAST_PID="$!"
  PIDS+=("$LAST_PID")
  wait_port "$rid-api" "$api"
  wait_port "$rid-route" "$route"
  wait_port "$rid-http" "$http"
}

start_workflow_provider() {
  local rid="$1"
  local workflow="$2"
  ZLINK_CPP_E2E_ROLE=provider \
  ZLINK_CPP_E2E_PROVIDER_RID="$rid" \
  ZLINK_CPP_E2E_PROVIDER_INSTANCE="$rid" \
  ZLINK_CPP_E2E_WORKFLOW_ENDPOINT="$workflow" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$SERVER" >"$LOG_DIR/$rid.stdout.log" 2>"$LOG_DIR/$rid.stderr.log" &
  LAST_PID="$!"
  PIDS+=("$LAST_PID")
  wait_port "$rid-workflow" "$workflow"
}

stop_pid() {
  local pid="$1"
  if kill -0 "$pid" >/dev/null 2>&1; then
    kill "$pid" >/dev/null 2>&1 || true
    wait "$pid" >/dev/null 2>&1 || true
  fi
}

wait_marker() {
  local file="$1"
  for _ in $(seq 1 200); do
    if [[ -f "$file" ]]; then
      return 0
    fi
    sleep 0.05
  done
  echo "Timed out waiting for marker $file" >&2
  return 1
}

run_client() {
  local scenario="$1"
  local suffix="$2"
  shift 2
  ZLINK_CPP_E2E_SCENARIO="$scenario" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_API_A_ENDPOINT="$API_A" \
  ZLINK_CPP_E2E_API_B_ENDPOINT="$API_B" \
  ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
  ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
  ZLINK_CPP_E2E_CLIENT_ROUTE_ENDPOINT="$CLIENT_ROUTE" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
  "$@" \
    "$CLIENT" >"$LOG_DIR/client-$suffix.stdout.log" 2>"$LOG_DIR/client-$suffix.stderr.log"
}

start_registry

start_provider api-a "$API_A" "$ROUTE_A" "$HTTP_A"
API_A_PID="$LAST_PID"
start_provider api-b "$API_B" "$ROUTE_B" "$HTTP_B"
API_B_PID="$LAST_PID"
start_workflow_provider workflow-a "$WORKFLOW_A"
WORKFLOW_A_PID="$LAST_PID"
sleep 1
run_client common common env
cat "$LOG_DIR/client-common.stdout.log"
stop_pid "$API_A_PID"
stop_pid "$API_B_PID"
stop_pid "$WORKFLOW_A_PID"

start_provider api-a "$API_A" "$ROUTE_A" "$HTTP_A" api-a ZLINK_CPP_E2E_SERVER_WEIGHT=75
API_A_PID="$LAST_PID"
start_provider api-b "$API_B" "$ROUTE_B" "$HTTP_B" api-b ZLINK_CPP_E2E_SERVER_WEIGHT=25
API_B_PID="$LAST_PID"
sleep 1
run_client weighted rm-c7 env
cat "$LOG_DIR/client-rm-c7.stdout.log"
stop_pid "$API_A_PID"
stop_pid "$API_B_PID"

start_provider api-a "$API_A" "$ROUTE_A" "$HTTP_A" api-a ZLINK_CPP_E2E_MAX_MESSAGE_SIZE=2048
API_A_PID="$LAST_PID"
sleep 1
run_client max-size rm-c8-max env
cat "$LOG_DIR/client-rm-c8-max.stdout.log"
stop_pid "$API_A_PID"

start_provider api-a "$API_A" "$ROUTE_A" "$HTTP_A"
API_A_PID="$LAST_PID"
READY="$LOG_DIR/rm-b1-ready"
CONTINUE="$LOG_DIR/rm-b1-continue"
run_client scale-out rm-b1 env \
  ZLINK_CPP_E2E_READY_FILE="$READY" \
  ZLINK_CPP_E2E_CONTINUE_FILE="$CONTINUE" &
B1_CLIENT_PID="$!"
wait_marker "$READY"
start_provider api-b "$API_B" "$ROUTE_B" "$HTTP_B"
API_B_PID="$LAST_PID"
sleep 5
touch "$CONTINUE"
wait "$B1_CLIENT_PID"
cat "$LOG_DIR/client-rm-b1.stdout.log"
stop_pid "$API_A_PID"
stop_pid "$API_B_PID"

start_provider api-a "$API_A" "$ROUTE_A" "$HTTP_A"
API_A_PID="$LAST_PID"
start_provider api-b "$API_B" "$ROUTE_B" "$HTTP_B"
API_B_PID="$LAST_PID"
READY="$LOG_DIR/rm-b2-ready"
CONTINUE="$LOG_DIR/rm-b2-continue"
run_client scale-in rm-b2 env \
  ZLINK_CPP_E2E_READY_FILE="$READY" \
  ZLINK_CPP_E2E_CONTINUE_FILE="$CONTINUE" &
B2_CLIENT_PID="$!"
wait_marker "$READY"
stop_pid "$API_B_PID"
sleep 5
touch "$CONTINUE"
wait "$B2_CLIENT_PID"
cat "$LOG_DIR/client-rm-b2.stdout.log"
stop_pid "$API_A_PID"

start_provider api-a "$API_A" "$ROUTE_A" "$HTTP_A" api-a-v1
API_A_PID="$LAST_PID"
READY="$LOG_DIR/rm-a4-ready"
CONTINUE="$LOG_DIR/rm-a4-continue"
run_client failover rm-a4 env \
  ZLINK_CPP_E2E_READY_FILE="$READY" \
  ZLINK_CPP_E2E_CONTINUE_FILE="$CONTINUE" &
A4_CLIENT_PID="$!"
wait_marker "$READY"
stop_pid "$API_A_PID"
start_provider api-a "$API_A2" "$ROUTE_A2" "$HTTP_A2" api-a-v2
API_A_PID="$LAST_PID"
sleep 5
touch "$CONTINUE"
wait "$A4_CLIENT_PID"
cat "$LOG_DIR/client-rm-a4.stdout.log"
stop_pid "$API_A_PID"
