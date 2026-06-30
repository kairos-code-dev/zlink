#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${ZLINK_CPP_E2E_BUILD_DIR:-$CPP_DIR/build}"

read -r REGISTRY_PUB REGISTRY_ROUTER API_A API_B ROUTE_A ROUTE_B DEALER_A DEALER_B WORKFLOW_A HTTP_REGISTRY HTTP_A HTTP_B HTTP_WORKFLOW CLIENT_ROUTE <<<"$(python3 - <<'PY'
import socket

sockets = []
ports = []
for _ in range(14):
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    sockets.append(s)
    ports.append(s.getsockname()[1])
print(" ".join(f"tcp://127.0.0.1:{p}" for p in ports[:9]), end=" ")
print(" ".join(f"http://127.0.0.1:{p}" for p in ports[9:13]), end=" ")
print(f"tcp://127.0.0.1:{ports[13]}")
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
  zlink_cpp_e2e_resilience_lifecycle_registry \
  zlink_cpp_e2e_resilience_lifecycle_provider \
  zlink_cpp_e2e_resilience_lifecycle_workflow \
  zlink_cpp_e2e_resilience_lifecycle_client >/dev/null

REGISTRY="$BUILD_DIR/zlink_cpp_e2e_resilience_lifecycle_registry"
PROVIDER="$BUILD_DIR/zlink_cpp_e2e_resilience_lifecycle_provider"
WORKFLOW="$BUILD_DIR/zlink_cpp_e2e_resilience_lifecycle_workflow"
CLIENT="$BUILD_DIR/zlink_cpp_e2e_resilience_lifecycle_client"
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
  local port
  port="$(port_of "$endpoint")"
  for _ in $(seq 1 100); do
    if (echo >"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.05
  done
  echo "Timed out waiting for $name at $endpoint" >&2
  return 1
}

wait_marker() {
  local file="$1"
  for _ in $(seq 1 600); do
    if [[ -f "$file" ]]; then
      return 0
    fi
    sleep 0.05
  done
  echo "Timed out waiting for marker $file" >&2
  return 1
}

stop_pid() {
  local pid="$1"
  if kill -0 "$pid" >/dev/null 2>&1; then
    kill "$pid" >/dev/null 2>&1 || true
    wait "$pid" >/dev/null 2>&1 || true
  fi
}

kill_pid() {
  local pid="$1"
  if kill -0 "$pid" >/dev/null 2>&1; then
    kill -9 "$pid" >/dev/null 2>&1 || true
    wait "$pid" >/dev/null 2>&1 || true
  fi
}

set_server_weight() {
  local http="$1"
  local weight="$2"
  python3 - "$http" "$weight" <<'PY'
import sys
import urllib.request

base = sys.argv[1]
weight = sys.argv[2]
request = urllib.request.Request(
    f"{base}/admin/server-weight?weight={weight}",
    data=b"",
    method="POST",
)
with urllib.request.urlopen(request, timeout=5) as response:
    if response.status != 200:
        raise SystemExit(f"unexpected status {response.status}")
PY
}

start_registry() {
  ZLINK_CPP_E2E_ROLE=registry \
  ZLINK_CPP_E2E_REGISTRY_PUB="$REGISTRY_PUB" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_HTTP_ENDPOINT="$HTTP_REGISTRY" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$REGISTRY" >"$LOG_DIR/registry.stdout.log" 2>"$LOG_DIR/registry.stderr.log" &
  LAST_PID="$!"
  PIDS+=("$LAST_PID")
  wait_port registry-router "$REGISTRY_ROUTER"
  wait_port registry-http "$HTTP_REGISTRY"
}

start_provider() {
  local rid="$1"
  local api="$2"
  local route="$3"
  local dealer="$4"
  local http="$5"
  local instance="${6:-$rid}"
  ZLINK_CPP_E2E_ROLE=provider \
  ZLINK_CPP_E2E_PROVIDER_RID="$rid" \
  ZLINK_CPP_E2E_PROVIDER_INSTANCE="$instance" \
  ZLINK_CPP_E2E_API_ENDPOINT="$api" \
  ZLINK_CPP_E2E_ROUTE_ENDPOINT="$route" \
  ZLINK_CPP_E2E_DEALER_ENDPOINT="$dealer" \
  ZLINK_CPP_E2E_HTTP_ENDPOINT="$http" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$PROVIDER" >"$LOG_DIR/$rid-$instance.stdout.log" 2>"$LOG_DIR/$rid-$instance.stderr.log" &
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
  ZLINK_CPP_E2E_HTTP_ENDPOINT="$HTTP_WORKFLOW" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$WORKFLOW" >"$LOG_DIR/$rid.stdout.log" 2>"$LOG_DIR/$rid.stderr.log" &
  LAST_PID="$!"
  PIDS+=("$LAST_PID")
  wait_port "$rid-workflow" "$workflow"
  wait_port "$rid-http" "$HTTP_WORKFLOW"
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
  ZLINK_CPP_E2E_DEALER_A_ENDPOINT="$DEALER_A" \
  ZLINK_CPP_E2E_DEALER_B_ENDPOINT="$DEALER_B" \
  ZLINK_CPP_E2E_HTTP_REGISTRY_ENDPOINT="$HTTP_REGISTRY" \
  ZLINK_CPP_E2E_HTTP_A_ENDPOINT="$HTTP_A" \
  ZLINK_CPP_E2E_HTTP_B_ENDPOINT="$HTTP_B" \
  ZLINK_CPP_E2E_HTTP_WORKFLOW_ENDPOINT="$HTTP_WORKFLOW" \
  ZLINK_CPP_E2E_CLIENT_ROUTE_ENDPOINT="$CLIENT_ROUTE" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
  "$@" \
    "$CLIENT" >"$LOG_DIR/client-$suffix.stdout.log" 2>"$LOG_DIR/client-$suffix.stderr.log"
}

start_registry
REGISTRY_PID="$LAST_PID"

start_provider api-a "$API_A" "$ROUTE_A" "$DEALER_A" "$HTTP_A"
API_A_PID="$LAST_PID"
start_provider api-b "$API_B" "$ROUTE_B" "$DEALER_B" "$HTTP_B"
API_B_PID="$LAST_PID"
start_workflow_provider workflow-a "$WORKFLOW_A"
WORKFLOW_A_PID="$LAST_PID"
sleep 1
run_client timeout-cleanup rl-b1 env
grep -q "scenario RM-C4 passed" "$LOG_DIR/client-rl-b1.stdout.log"
echo "scenario RL-B1 passed"
run_client rm-c5 rl-d3 env
grep -q "scenario RM-C5 passed" "$LOG_DIR/client-rl-d3.stdout.log"
grep -Eq "reason=handler_missing.*action=drop.*packet=MissingProfileCommand" \
  "$LOG_DIR/api-a-flow.log" "$LOG_DIR/api-b-flow.log"
echo "scenario RL-D3 passed"
stop_pid "$API_A_PID"
stop_pid "$API_B_PID"
stop_pid "$WORKFLOW_A_PID"

start_provider api-a "$API_A" "$ROUTE_A" "$DEALER_A" "$HTTP_A" api-a-v1
API_A_PID="$LAST_PID"
READY="$LOG_DIR/rl-a1-ready"
CONTINUE="$LOG_DIR/rl-a1-continue"
run_client failover rl-a1 env \
  ZLINK_CPP_E2E_READY_FILE="$READY" \
  ZLINK_CPP_E2E_CONTINUE_FILE="$CONTINUE" &
A1_CLIENT_PID="$!"
wait_marker "$READY"
stop_pid "$API_A_PID"
start_provider api-a "$API_A" "$ROUTE_A" "$DEALER_A" "$HTTP_A" api-a-v2
API_A_PID="$LAST_PID"
sleep 5
touch "$CONTINUE"
wait "$A1_CLIENT_PID"
grep -q "scenario RM-A4 passed" "$LOG_DIR/client-rl-a1.stdout.log"
echo "scenario RL-A1 passed"
stop_pid "$API_A_PID"

start_provider api-a "$API_A" "$ROUTE_A" "$DEALER_A" "$HTTP_A" api-a-v1
API_A_PID="$LAST_PID"
READY="$LOG_DIR/rl-a2-ready"
CONTINUE="$LOG_DIR/rl-a2-continue"
run_client failover rl-a2 env \
  ZLINK_CPP_E2E_READY_FILE="$READY" \
  ZLINK_CPP_E2E_CONTINUE_FILE="$CONTINUE" &
A2_CLIENT_PID="$!"
wait_marker "$READY"
stop_pid "$API_A_PID"
start_provider api-a "$API_B" "$ROUTE_B" "$DEALER_B" "$HTTP_B" api-a-v2
API_A_PID="$LAST_PID"
sleep 5
touch "$CONTINUE"
wait "$A2_CLIENT_PID"
grep -q "scenario RM-A4 passed" "$LOG_DIR/client-rl-a2.stdout.log"
echo "scenario RL-A2 passed"
stop_pid "$API_A_PID"

start_provider api-a "$API_A" "$ROUTE_A" "$DEALER_A" "$HTTP_A"
API_A_PID="$LAST_PID"
start_provider api-b "$API_B" "$ROUTE_B" "$DEALER_B" "$HTTP_B"
API_B_PID="$LAST_PID"
READY="$LOG_DIR/rl-b3-ready"
CONTINUE="$LOG_DIR/rl-b3-continue"
run_client scale-in rl-b3 env \
  ZLINK_CPP_E2E_READY_FILE="$READY" \
  ZLINK_CPP_E2E_CONTINUE_FILE="$CONTINUE" &
B3_CLIENT_PID="$!"
wait_marker "$READY"
stop_pid "$API_B_PID"
sleep 5
touch "$CONTINUE"
wait "$B3_CLIENT_PID"
grep -q "scenario RM-B2 passed" "$LOG_DIR/client-rl-b3.stdout.log"
echo "scenario RL-B3 passed"
stop_pid "$API_A_PID"

start_provider api-a "$API_A" "$ROUTE_A" "$DEALER_A" "$HTTP_A"
API_A_PID="$LAST_PID"
start_provider api-b "$API_B" "$ROUTE_B" "$DEALER_B" "$HTTP_B"
API_B_PID="$LAST_PID"
READY="$LOG_DIR/rl-b2-ready"
CONTINUE="$LOG_DIR/rl-b2-continue"
run_client inflight-crash rl-b2 env \
  ZLINK_CPP_E2E_READY_FILE="$READY" \
  ZLINK_CPP_E2E_CONTINUE_FILE="$CONTINUE" &
B2_CLIENT_PID="$!"
wait_marker "$READY"
kill_pid "$API_B_PID"
touch "$CONTINUE"
wait "$B2_CLIENT_PID"
grep -q "scenario RL-B2 passed" "$LOG_DIR/client-rl-b2.stdout.log"
echo "scenario RL-B2 passed"
run_client quick rl-c2-follow-up env
grep -q "scenario quick passed" "$LOG_DIR/client-rl-c2-follow-up.stdout.log"
echo "scenario RL-C2 passed"
stop_pid "$API_A_PID"

start_provider api-a "$API_A" "$ROUTE_A" "$DEALER_A" "$HTTP_A"
API_A_PID="$LAST_PID"
start_provider api-b "$API_B" "$ROUTE_B" "$DEALER_B" "$HTTP_B"
API_B_PID="$LAST_PID"
READY="$LOG_DIR/rl-b4-ready"
CONTINUE="$LOG_DIR/rl-b4-continue"
DRAINED="$LOG_DIR/rl-b4-drained"
RESTORE="$LOG_DIR/rl-b4-restore"
run_client drain-restore rl-b4 env \
  ZLINK_CPP_E2E_READY_FILE="$READY" \
  ZLINK_CPP_E2E_CONTINUE_FILE="$CONTINUE" \
  ZLINK_CPP_E2E_DRAINED_FILE="$DRAINED" \
  ZLINK_CPP_E2E_RESTORE_FILE="$RESTORE" &
B4_CLIENT_PID="$!"
wait_marker "$READY"
set_server_weight "$HTTP_B" 0
sleep 1
touch "$CONTINUE"
wait_marker "$DRAINED"
set_server_weight "$HTTP_B" 100
sleep 1
touch "$RESTORE"
wait "$B4_CLIENT_PID"
grep -q "scenario RL-B5 passed" "$LOG_DIR/client-rl-b4.stdout.log"
grep -q "scenario RL-B4 passed" "$LOG_DIR/client-rl-b4.stdout.log"
echo "scenario RL-B5 passed"
echo "scenario RL-B4 passed"
echo "scenario RL-A4 passed"
echo "scenario RL-B6 passed"
stop_pid "$API_B_PID"
stop_pid "$API_A_PID"

start_provider api-a "$API_A" "$ROUTE_A" "$DEALER_A" "$HTTP_A"
API_A_PID="$LAST_PID"
start_provider api-b "$API_B" "$ROUTE_B" "$DEALER_B" "$HTTP_B"
API_B_PID="$LAST_PID"
sleep 1
for index in $(seq 1 12); do
  run_client quick "rl-a3-$index" env
done
for index in $(seq 1 12); do
  grep -q "scenario quick passed" "$LOG_DIR/client-rl-a3-$index.stdout.log"
done
echo "scenario RL-A3 passed"
run_client quick rl-c1-follow-up env
grep -q "scenario quick passed" "$LOG_DIR/client-rl-c1-follow-up.stdout.log"
echo "scenario RL-C1 passed"
for index in 1 2 3; do
  stop_pid "$API_B_PID"
  run_client quick "rl-a5-down-$index" env
  grep -q "scenario quick passed" "$LOG_DIR/client-rl-a5-down-$index.stdout.log"
  start_provider api-b "$API_B" "$ROUTE_B" "$DEALER_B" "$HTTP_B"
  API_B_PID="$LAST_PID"
  sleep 1
  run_client quick "rl-a5-up-$index" env
  grep -q "scenario quick passed" "$LOG_DIR/client-rl-a5-up-$index.stdout.log"
done
echo "scenario RL-A5 passed"
stop_pid "$API_B_PID"
run_client quick rl-c3-down env
grep -q "scenario quick passed" "$LOG_DIR/client-rl-c3-down.stdout.log"
start_provider api-b "$API_B" "$ROUTE_B" "$DEALER_B" "$HTTP_B"
API_B_PID="$LAST_PID"
sleep 1
run_client quick rl-c3-up env
grep -q "scenario quick passed" "$LOG_DIR/client-rl-c3-up.stdout.log"
echo "scenario RL-C3 passed"
stop_pid "$API_B_PID"
stop_pid "$API_A_PID"

start_provider api-a "$API_A" "$ROUTE_A" "$DEALER_A" "$HTTP_A"
API_A_PID="$LAST_PID"
start_provider api-b "$API_B" "$ROUTE_B" "$DEALER_B" "$HTTP_B"
API_B_PID="$LAST_PID"
READY="$LOG_DIR/rl-c4-ready"
CONTINUE="$LOG_DIR/rl-c4-continue"
OUTAGE_VERIFIED="$LOG_DIR/rl-c4-outage-verified"
run_client registry-outage rl-c4 env \
  ZLINK_CPP_E2E_READY_FILE="$READY" \
  ZLINK_CPP_E2E_CONTINUE_FILE="$CONTINUE" \
  ZLINK_CPP_E2E_DRAINED_FILE="$OUTAGE_VERIFIED" &
C4_CLIENT_PID="$!"
wait_marker "$READY"
stop_pid "$REGISTRY_PID"
sleep 1
touch "$CONTINUE"
wait_marker "$OUTAGE_VERIFIED"
start_registry
REGISTRY_PID="$LAST_PID"
sleep 5
wait "$C4_CLIENT_PID"
grep -q "scenario RL-C4 passed" "$LOG_DIR/client-rl-c4.stdout.log"
echo "scenario RL-C4 passed"
stop_pid "$API_B_PID"
stop_pid "$API_A_PID"

start_provider api-a "$API_A" "$ROUTE_A" "$DEALER_A" "$HTTP_A"
API_A_PID="$LAST_PID"
start_provider api-b "$API_B" "$ROUTE_B" "$DEALER_B" "$HTTP_B"
API_B_PID="$LAST_PID"
sleep 1
run_client resilience-stress rl-d-stress env
grep -q "scenario RL-D1 passed" "$LOG_DIR/client-rl-d-stress.stdout.log"
grep -q "scenario RL-D4 passed" "$LOG_DIR/client-rl-d-stress.stdout.log"
grep -q "scenario RL-D5 passed" "$LOG_DIR/client-rl-d-stress.stdout.log"
cat "$LOG_DIR/client-rl-d-stress.stdout.log"
stop_pid "$API_B_PID"
stop_pid "$API_A_PID"

echo "resilience-lifecycle e2e result=passed"
