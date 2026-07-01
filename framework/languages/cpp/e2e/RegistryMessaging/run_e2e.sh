#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${ZLINK_CPP_E2E_BUILD_DIR:-$CPP_DIR/build}"

read -r REGISTRY_PUB REGISTRY_ROUTER API_A API_B ROUTE_A ROUTE_B WORKFLOW_A HTTP_REGISTRY HTTP_A HTTP_B HTTP_WORKFLOW HTTP_DIRECT_CONSUMER HTTP_SINGLE_CONSUMER HTTP_DISCOVERY_CONSUMER HTTP_BACKPRESSURE_CONSUMER CLIENT_ROUTE API_A2 ROUTE_A2 HTTP_A2 <<<"$(python3 - <<'PY'
import socket

sockets = []
ports = []
for _ in range(19):
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    sockets.append(s)
    ports.append(s.getsockname()[1])
print(" ".join(f"tcp://127.0.0.1:{p}" for p in ports[:7]), end=" ")
print(" ".join(f"http://127.0.0.1:{p}" for p in ports[7:15]), end=" ")
print(f"tcp://127.0.0.1:{ports[15]}", end=" ")
print(" ".join(f"tcp://127.0.0.1:{p}" for p in ports[16:18]), end=" ")
print(f"http://127.0.0.1:{ports[18]}")
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
  zlink_cpp_e2e_registry_messaging_registry \
  zlink_cpp_e2e_registry_messaging_provider \
  zlink_cpp_e2e_registry_messaging_workflow \
  zlink_cpp_e2e_registry_messaging_consumer \
  zlink_cpp_e2e_registry_messaging_client >/dev/null

REGISTRY_SERVER="$BUILD_DIR/zlink_cpp_e2e_registry_messaging_registry"
PROVIDER_SERVER="$BUILD_DIR/zlink_cpp_e2e_registry_messaging_provider"
WORKFLOW_SERVER="$BUILD_DIR/zlink_cpp_e2e_registry_messaging_workflow"
CONSUMER_SERVER="$BUILD_DIR/zlink_cpp_e2e_registry_messaging_consumer"
CLIENT="$BUILD_DIR/zlink_cpp_e2e_registry_messaging_client"
SCENARIO="${1:-all}"
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
  for _ in $(seq 1 60); do
    if (echo >"/dev/tcp/${host}/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.05
  done
  echo "Timed out waiting for $name at $endpoint" >&2
  return 1
}

start_registry() {
  ZLINK_CPP_E2E_REGISTRY_PUB="$REGISTRY_PUB" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_HTTP_ENDPOINT="$HTTP_REGISTRY" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$REGISTRY_SERVER" >"$LOG_DIR/registry.stdout.log" 2>"$LOG_DIR/registry.stderr.log" &
  PIDS+=("$!")
  wait_port registry-router "$REGISTRY_ROUTER"
  wait_port registry-http "$HTTP_REGISTRY"
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
    ZLINK_CPP_E2E_PROVIDER_RID="$rid" \
    ZLINK_CPP_E2E_PROVIDER_INSTANCE="$instance" \
    ZLINK_CPP_E2E_API_ENDPOINT="$api" \
    ZLINK_CPP_E2E_ROUTE_ENDPOINT="$route" \
    ZLINK_CPP_E2E_HTTP_ENDPOINT="$http" \
    ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
    ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$PROVIDER_SERVER" >"$LOG_DIR/$rid.stdout.log" 2>"$LOG_DIR/$rid.stderr.log" &
  LAST_PID="$!"
  PIDS+=("$LAST_PID")
  wait_port "$rid-api" "$api"
  wait_port "$rid-route" "$route"
  wait_port "$rid-http" "$http"
}

start_workflow_provider() {
  local rid="$1"
  local workflow="$2"
  ZLINK_CPP_E2E_PROVIDER_RID="$rid" \
  ZLINK_CPP_E2E_PROVIDER_INSTANCE="$rid" \
  ZLINK_CPP_E2E_WORKFLOW_ENDPOINT="$workflow" \
  ZLINK_CPP_E2E_HTTP_ENDPOINT="$HTTP_WORKFLOW" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$WORKFLOW_SERVER" >"$LOG_DIR/$rid.stdout.log" 2>"$LOG_DIR/$rid.stderr.log" &
  LAST_PID="$!"
  PIDS+=("$LAST_PID")
  wait_port "$rid-workflow" "$workflow"
  wait_port "$rid-http" "$HTTP_WORKFLOW"
}

start_consumer() {
  local name="$1"
  local http="$2"
  local endpoints="$3"
  local registry_router="$4"
  ZLINK_CPP_E2E_HTTP_ENDPOINT="$http" \
  ZLINK_CPP_E2E_PROVIDER_ENDPOINTS="$endpoints" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$registry_router" \
  ZLINK_CPP_E2E_TRACE_LABEL="$name" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$CONSUMER_SERVER" >"$LOG_DIR/$name.stdout.log" 2>"$LOG_DIR/$name.stderr.log" &
  LAST_PID="$!"
  PIDS+=("$LAST_PID")
  wait_port "$name-http" "$http"
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
  for _ in $(seq 1 60); do
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
  ZLINK_CPP_E2E_HTTP_A_ENDPOINT="$HTTP_A" \
  ZLINK_CPP_E2E_HTTP_B_ENDPOINT="$HTTP_B" \
  ZLINK_CPP_E2E_HTTP_WORKFLOW_ENDPOINT="$HTTP_WORKFLOW" \
  ZLINK_CPP_E2E_DIRECT_CONSUMER_URL="$HTTP_DIRECT_CONSUMER" \
  ZLINK_CPP_E2E_SINGLE_CONSUMER_URL="$HTTP_SINGLE_CONSUMER" \
  ZLINK_CPP_E2E_DISCOVERY_CONSUMER_URL="$HTTP_DISCOVERY_CONSUMER" \
  ZLINK_CPP_E2E_BACKPRESSURE_CONSUMER_URL="$HTTP_BACKPRESSURE_CONSUMER" \
  ZLINK_CPP_E2E_CLIENT_ROUTE_ENDPOINT="$CLIENT_ROUTE" \
  ZLINK_CPP_E2E_HTTP_REGISTRY_ENDPOINT="$HTTP_REGISTRY" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
  "$@" \
    "$CLIENT" >"$LOG_DIR/client-$suffix.stdout.log" 2>"$LOG_DIR/client-$suffix.stderr.log"
}

if [[ "$SCENARIO" == "all" ]]; then
  CHILD_LOG_MANIFEST="$LOG_DIR/child-runs.log"
  for scenario in RM-A1 RM-A2 RM-A4 RM-A6 RM-B1 RM-B2 RM-C1 RM-C2 RM-C3 RM-C4 RM-C5 RM-C7 RM-C8 RM-C9; do
    echo "running $scenario"
    child_output="$LOG_DIR/child-$scenario.output.log"
    ZLINK_CPP_E2E_BUILD_DIR="$BUILD_DIR" "$0" "$scenario" | tee "$child_output"
    child_log_dir="$(sed -n 's/^log_dir=//p' "$child_output" | tail -1)"
    if [[ -z "$child_log_dir" || ! -d "$child_log_dir" ]]; then
      echo "missing child log directory for $scenario" >&2
      exit 1
    fi
    echo "$scenario $child_log_dir" >>"$CHILD_LOG_MANIFEST"
  done
  echo "registry-messaging e2e result=passed"
  exit 0
fi

case "$SCENARIO" in
  RM-A1|rm-a1|RM-A2|rm-a2|RM-A4|rm-a4|RM-A6|rm-a6|RM-B1|rm-b1|RM-B2|rm-b2|RM-C1|rm-c1|RM-C2|rm-c2|RM-C3|rm-c3|RM-C4|rm-c4|RM-C5|rm-c5|RM-C7|rm-c7|RM-C8|rm-c8|RM-C9|rm-c9)
    ;;
  *)
    echo "Unknown RegistryMessaging scenario: $SCENARIO" >&2
    exit 1
    ;;
esac

start_registry

if [[ "$SCENARIO" == "RM-A2" || "$SCENARIO" == "rm-a2" ]]; then
  start_provider api-a "$API_A" "$ROUTE_A" "$HTTP_A"
  API_A_PID="$LAST_PID"
  sleep 1
  run_client rm-a2 rm-a2 env
  cat "$LOG_DIR/client-rm-a2.stdout.log"
  exit 0
fi

if [[ "$SCENARIO" == "RM-A1" || "$SCENARIO" == "rm-a1" ]]; then
  start_provider api-a "$API_A" "$ROUTE_A" "$HTTP_A"
  API_A_PID="$LAST_PID"
  start_provider api-b "$API_B" "$ROUTE_B" "$HTTP_B"
  API_B_PID="$LAST_PID"
  sleep 1
  run_client rm-a1 rm-a1 env
  cat "$LOG_DIR/client-rm-a1.stdout.log"
  exit 0
fi

if [[ "$SCENARIO" == "RM-A4" || "$SCENARIO" == "rm-a4" ]]; then
  start_provider api-a "$API_A" "$ROUTE_A" "$HTTP_A" api-a-v1
  API_A_PID="$LAST_PID"
  READY="$LOG_DIR/rm-a4-ready"
  CONTINUE="$LOG_DIR/rm-a4-continue"
  run_client rm-a4 rm-a4 env \
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
  exit 0
fi

if [[ "$SCENARIO" == "RM-A6" || "$SCENARIO" == "rm-a6" ]]; then
  start_provider api-a "$API_A" "$ROUTE_A" "$HTTP_A"
  API_A_PID="$LAST_PID"
  start_provider api-b "$API_B" "$ROUTE_B" "$HTTP_B"
  API_B_PID="$LAST_PID"
  start_workflow_provider workflow-a "$WORKFLOW_A"
  WORKFLOW_A_PID="$LAST_PID"
  sleep 1
  run_client rm-a6 rm-a6 env
  cat "$LOG_DIR/client-rm-a6.stdout.log"
  exit 0
fi

if [[ "$SCENARIO" == "RM-B1" || "$SCENARIO" == "rm-b1" ]]; then
  start_provider api-a "$API_A" "$ROUTE_A" "$HTTP_A"
  API_A_PID="$LAST_PID"
  READY="$LOG_DIR/rm-b1-ready"
  CONTINUE="$LOG_DIR/rm-b1-continue"
  run_client rm-b1 rm-b1 env \
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
  exit 0
fi

if [[ "$SCENARIO" == "RM-B2" || "$SCENARIO" == "rm-b2" ]]; then
  start_provider api-a "$API_A" "$ROUTE_A" "$HTTP_A"
  API_A_PID="$LAST_PID"
  start_provider api-b "$API_B" "$ROUTE_B" "$HTTP_B"
  API_B_PID="$LAST_PID"
  READY="$LOG_DIR/rm-b2-ready"
  CONTINUE="$LOG_DIR/rm-b2-continue"
  run_client rm-b2 rm-b2 env \
    ZLINK_CPP_E2E_READY_FILE="$READY" \
    ZLINK_CPP_E2E_CONTINUE_FILE="$CONTINUE" &
  B2_CLIENT_PID="$!"
  wait_marker "$READY"
  stop_pid "$API_B_PID"
  sleep 5
  touch "$CONTINUE"
  wait "$B2_CLIENT_PID"
  cat "$LOG_DIR/client-rm-b2.stdout.log"
  exit 0
fi

if [[ "$SCENARIO" == "RM-C1" || "$SCENARIO" == "rm-c1" ]]; then
  start_provider api-a "$API_A" "$ROUTE_A" "$HTTP_A"
  API_A_PID="$LAST_PID"
  start_provider api-b "$API_B" "$ROUTE_B" "$HTTP_B"
  API_B_PID="$LAST_PID"
  sleep 1
  run_client rm-c1 rm-c1 env
  cat "$LOG_DIR/client-rm-c1.stdout.log"
  exit 0
fi

if [[ "$SCENARIO" == "RM-C2" || "$SCENARIO" == "rm-c2" ]]; then
  start_provider api-a "$API_A" "$ROUTE_A" "$HTTP_A"
  API_A_PID="$LAST_PID"
  start_provider api-b "$API_B" "$ROUTE_B" "$HTTP_B"
  API_B_PID="$LAST_PID"
  sleep 1
  run_client rm-c2 rm-c2 env
  cat "$LOG_DIR/client-rm-c2.stdout.log"
  exit 0
fi

if [[ "$SCENARIO" == "RM-C3" || "$SCENARIO" == "rm-c3" ]]; then
  start_provider api-a "$API_A" "$ROUTE_A" "$HTTP_A"
  API_A_PID="$LAST_PID"
  start_provider api-b "$API_B" "$ROUTE_B" "$HTTP_B"
  API_B_PID="$LAST_PID"
  start_consumer direct-consumer "$HTTP_DIRECT_CONSUMER" "$API_A,$API_B" ""
  DIRECT_CONSUMER_PID="$LAST_PID"
  sleep 1
  run_client rm-c3 rm-c3 env
  cat "$LOG_DIR/client-rm-c3.stdout.log"
  exit 0
fi

if [[ "$SCENARIO" == "RM-C4" || "$SCENARIO" == "rm-c4" ]]; then
  start_provider api-a "$API_A" "$ROUTE_A" "$HTTP_A"
  API_A_PID="$LAST_PID"
  start_provider api-b "$API_B" "$ROUTE_B" "$HTTP_B"
  API_B_PID="$LAST_PID"
  start_consumer discovery-consumer "$HTTP_DISCOVERY_CONSUMER" "" "$REGISTRY_ROUTER"
  DISCOVERY_CONSUMER_PID="$LAST_PID"
  sleep 1
  run_client rm-c4 rm-c4 env
  cat "$LOG_DIR/client-rm-c4.stdout.log"
  exit 0
fi

if [[ "$SCENARIO" == "RM-C5" || "$SCENARIO" == "rm-c5" ]]; then
  start_provider api-a "$API_A" "$ROUTE_A" "$HTTP_A"
  API_A_PID="$LAST_PID"
  start_provider api-b "$API_B" "$ROUTE_B" "$HTTP_B"
  API_B_PID="$LAST_PID"
  start_consumer discovery-consumer "$HTTP_DISCOVERY_CONSUMER" "" "$REGISTRY_ROUTER"
  DISCOVERY_CONSUMER_PID="$LAST_PID"
  sleep 1
  run_client rm-c5 rm-c5 env
  cat "$LOG_DIR/client-rm-c5.stdout.log"
  exit 0
fi

if [[ "$SCENARIO" == "RM-C7" || "$SCENARIO" == "rm-c7" ]]; then
  start_provider api-a "$API_A" "$ROUTE_A" "$HTTP_A" api-a ZLINK_CPP_E2E_SERVER_WEIGHT=75
  API_A_PID="$LAST_PID"
  start_provider api-b "$API_B" "$ROUTE_B" "$HTTP_B" api-b ZLINK_CPP_E2E_SERVER_WEIGHT=25
  API_B_PID="$LAST_PID"
  sleep 1
  run_client rm-c7 rm-c7 env
  cat "$LOG_DIR/client-rm-c7.stdout.log"
  exit 0
fi

if [[ "$SCENARIO" == "RM-C8" || "$SCENARIO" == "rm-c8" ]]; then
  start_provider api-a "$API_A" "$ROUTE_A" "$HTTP_A"
  API_A_PID="$LAST_PID"
  start_provider api-b "$API_B" "$ROUTE_B" "$HTTP_B"
  API_B_PID="$LAST_PID"
  start_consumer single-consumer "$HTTP_SINGLE_CONSUMER" "$API_A" ""
  SINGLE_CONSUMER_PID="$LAST_PID"
  sleep 1
  run_client rm-c8 rm-c8 env
  cat "$LOG_DIR/client-rm-c8.stdout.log"
  stop_pid "$SINGLE_CONSUMER_PID"
  stop_pid "$API_A_PID"
  stop_pid "$API_B_PID"

  start_provider api-a "$API_A" "$ROUTE_A" "$HTTP_A" api-a ZLINK_CPP_E2E_MAX_MESSAGE_SIZE=2048
  API_A_PID="$LAST_PID"
  start_consumer single-consumer-max "$HTTP_SINGLE_CONSUMER" "$API_A" ""
  SINGLE_CONSUMER_PID="$LAST_PID"
  sleep 1
  run_client rm-c8-max rm-c8-max env
  cat "$LOG_DIR/client-rm-c8-max.stdout.log"
  exit 0
fi

if [[ "$SCENARIO" == "RM-C9" || "$SCENARIO" == "rm-c9" ]]; then
  start_provider api-a "$API_A" "$ROUTE_A" "$HTTP_A"
  API_A_PID="$LAST_PID"
  start_consumer backpressure-consumer "$HTTP_BACKPRESSURE_CONSUMER" "$API_A" ""
  BACKPRESSURE_CONSUMER_PID="$LAST_PID"
  sleep 1
  run_client rm-c9 rm-c9 env
  cat "$LOG_DIR/client-rm-c9.stdout.log"
  exit 0
fi

echo "Unknown RegistryMessaging scenario: $SCENARIO" >&2
exit 1
