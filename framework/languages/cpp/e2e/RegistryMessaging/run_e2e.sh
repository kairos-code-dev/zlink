#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
source "$SCRIPT_DIR/../redis-common.sh"
BUILD_DIR="${ZLINK_CPP_E2E_BUILD_DIR:-$CPP_DIR/build}"
E2E_START_ORDER="${E2E_START_ORDER:-forward}"
LOCAL_READINESS_TIMEOUT_SECONDS=3
LOCAL_READINESS_POLL_SECONDS=0.1
PROCESS_SHUTDOWN_TIMEOUT_SECONDS=15
ROUTE_SETTLE_SECONDS=5
SCENARIO_SETTLE_SECONDS=3
HTTP_PROBE_TIMEOUT_SECONDS=3
REDIS_READINESS_TIMEOUT_SECONDS=30
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
  python3 - "$PROCESS_SHUTDOWN_TIMEOUT_SECONDS" "$LOCAL_READINESS_POLL_SECONDS" <<'PY'
import math
import sys

timeout = float(sys.argv[1])
poll = float(sys.argv[2])
print(max(1, math.ceil(timeout / poll)))
PY
)"

read -r API_A API_B ROUTE_A ROUTE_B WORKFLOW_A HTTP_A HTTP_B HTTP_WORKFLOW HTTP_DIRECT_CONSUMER HTTP_SINGLE_CONSUMER HTTP_STORE_CONSUMER HTTP_BACKPRESSURE_CONSUMER CLIENT_ROUTE API_A2 ROUTE_A2 HTTP_A2 <<<"$(python3 - <<'PY'
import socket

sockets = []
ports = []
for _ in range(16):
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    sockets.append(s)
    ports.append(s.getsockname()[1])
print(" ".join(f"tcp://127.0.0.1:{p}" for p in ports[:5]), end=" ")
print(" ".join(f"http://127.0.0.1:{p}" for p in ports[5:12]), end=" ")
print(f"tcp://127.0.0.1:{ports[12]}", end=" ")
print(" ".join(f"tcp://127.0.0.1:{p}" for p in ports[13:15]), end=" ")
print(f"http://127.0.0.1:{ports[15]}")
for s in sockets:
    s.close()
PY
)"

RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$SCRIPT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"
echo "log_dir=$LOG_DIR"
echo "start_order=$E2E_START_ORDER"

ordered_roles() {
  python3 - "$E2E_START_ORDER" "$@" <<'PY'
import random
import sys

mode = sys.argv[1]
roles = sys.argv[2:]
if mode in ("", "forward"):
    pass
elif mode == "reverse":
    roles.reverse()
elif mode.startswith("shuffle:"):
    seed_text = mode.split(":", 1)[1]
    if seed_text == "":
        raise SystemExit("E2E_START_ORDER shuffle requires a seed")
    random.Random(int(seed_text)).shuffle(roles)
else:
    raise SystemExit(f"unsupported E2E_START_ORDER={mode!r}")
for role in roles:
    print(role)
PY
}

wait_tcp() {
  local host="$1"
  local port="$2"
  local name="$3"
  if python3 - "$host" "$port" "$REDIS_READINESS_TIMEOUT_SECONDS" <<'PY'
import socket
import sys
import time

host, port, timeout = sys.argv[1], int(sys.argv[2]), float(sys.argv[3])
deadline = time.monotonic() + timeout
while time.monotonic() < deadline:
    try:
        with socket.create_connection((host, port), timeout=1):
            sys.exit(0)
    except OSError:
        time.sleep(0.2)
sys.exit(1)
PY
  then
    return 0
  fi
  echo "Timed out waiting ${REDIS_READINESS_TIMEOUT_SECONDS}s for $name at $host:$port" >&2
  return 1
}

REDIS_CONTAINER=""
REDIS_CONTAINER_OWNED=0
REDIS_KEY_PREFIX="zlink:e2e:cfg1:$(date +%s)-$$"
if [[ -n "${ZLINK_CPP_E2E_OWNED_REDIS_CONTAINER:-}" && -n "${ZLINK_REDIS_E2E_ENDPOINT:-}" ]]; then
  REDIS_ENDPOINT="$ZLINK_REDIS_E2E_ENDPOINT"
  REDIS_CONTAINER="$ZLINK_CPP_E2E_OWNED_REDIS_CONTAINER"
  echo "redis endpoint=$REDIS_ENDPOINT (existing owned container $REDIS_CONTAINER)"
elif [[ -n "${ZLINK_REDIS_E2E_ENDPOINT:-}" ]]; then
  echo "External Redis endpoint is not supported by the C++ RegistryMessaging e2e runner." >&2
  exit 2
else
  zlink_redis_start_scoped_assign REDIS_CONTAINER redis_port \
    "zlink-redis-cpp-e2e-registrymessaging" "redis:7-alpine"
  REDIS_CONTAINER_OWNED=1
  REDIS_ENDPOINT="127.0.0.1:${redis_port}"
  echo "redis endpoint=$REDIS_ENDPOINT (container $REDIS_CONTAINER)"
fi
REDIS_HOST="${REDIS_ENDPOINT%:*}"
REDIS_TCP_PORT="${REDIS_ENDPOINT##*:}"
wait_tcp "$REDIS_HOST" "$REDIS_TCP_PORT" redis
echo "redis key prefix=$REDIS_KEY_PREFIX"

cmake -S "$CPP_DIR" -B "$BUILD_DIR" >/dev/null
cmake --build "$BUILD_DIR" --target \
  zlink_cpp_e2e_registry_messaging_provider \
  zlink_cpp_e2e_registry_messaging_workflow \
  zlink_cpp_e2e_registry_messaging_consumer \
  zlink_cpp_e2e_registry_messaging_client >/dev/null

PROVIDER_SERVER="$BUILD_DIR/zlink_cpp_e2e_registry_messaging_provider"
WORKFLOW_SERVER="$BUILD_DIR/zlink_cpp_e2e_registry_messaging_workflow"
CONSUMER_SERVER="$BUILD_DIR/zlink_cpp_e2e_registry_messaging_consumer"
CLIENT="$BUILD_DIR/zlink_cpp_e2e_registry_messaging_client"
SCENARIO="${1:-all}"
PIDS=()
LAST_PID=""

wait_pid_status() {
  local pid="$1"
  local label="$2"
  local status
  set +e
  wait "$pid"
  status=$?
  set -e
  if [[ "$status" -eq 127 ]]; then
    return 0
  fi
  if [[ "$status" -eq 0 || "$status" -eq 130 || "$status" -eq 143 ]]; then
    return 0
  fi
  echo "$label exited unexpectedly with status $status" >&2
  return 1
}

terminate_pid() {
  local pid="$1"
  local label="${2:-process $pid}"
  local state
  if [[ -z "$pid" ]]; then
    return 0
  fi
  if ! kill -0 "$pid" >/dev/null 2>&1; then
    wait_pid_status "$pid" "$label"
    return $?
  fi
  kill "$pid" >/dev/null 2>&1 || true
  for _ in $(seq 1 "$PROCESS_SHUTDOWN_ATTEMPTS"); do
    state="$(ps -o stat= -p "$pid" 2>/dev/null | awk '{print $1}')"
    if [[ -z "$state" || "$state" == Z* ]]; then
      wait_pid_status "$pid" "$label"
      return $?
    fi
    sleep 0.1
  done
  kill -9 "$pid" >/dev/null 2>&1 || true
  wait_pid_status "$pid" "$label"
}

cleanup() {
  local code=$?
  local cleanup_failed=0
  for pid in "${PIDS[@]:-}"; do
    if [[ -z "$pid" ]]; then
      continue
    fi
    if ! terminate_pid "$pid" "cleanup process $pid"; then
      cleanup_failed=1
    fi
  done
  if [[ -n "$REDIS_CONTAINER" && "$REDIS_CONTAINER_OWNED" == "1" ]]; then
    docker rm -fv "$REDIS_CONTAINER" >/dev/null 2>&1 || true
  fi
  if [[ $code -ne 0 ]]; then
    echo "E2E failed. Logs: $LOG_DIR" >&2
  elif [[ $cleanup_failed -ne 0 ]]; then
    echo "E2E cleanup failed. Logs: $LOG_DIR" >&2
    code=1
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
  for _ in $(seq 1 "$LOCAL_READINESS_ATTEMPTS"); do
    if (echo >"/dev/tcp/${host}/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep "$LOCAL_READINESS_POLL_SECONDS"
  done
  echo "Timed out waiting ${LOCAL_READINESS_TIMEOUT_SECONDS}s for $name at $endpoint" >&2
  return 1
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
    ZLINK_CPP_E2E_ROUTE_PEERS="$ROUTE_A,$ROUTE_B" \
    ZLINK_CPP_E2E_HTTP_ENDPOINT="$http" \
    ZLINK_CPP_E2E_REDIS_ENDPOINT="$REDIS_ENDPOINT" \
    ZLINK_CPP_E2E_REDIS_KEY_PREFIX="$REDIS_KEY_PREFIX" \
    ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$PROVIDER_SERVER" >"$LOG_DIR/$rid.stdout.log" 2>"$LOG_DIR/$rid.stderr.log" &
  LAST_PID="$!"
  PIDS+=("$LAST_PID")
  wait_port "$rid-api" "$api"
  wait_port "$rid-route" "$route"
  wait_port "$rid-http" "$http"
}

start_standard_provider_pair() {
  local role
  mapfile -t ordered_server_roles < <(ordered_roles api-a api-b)
  for role in "${ordered_server_roles[@]}"; do
    echo "starting_role=$role"
    case "$role" in
      api-a)
        start_provider api-a "$API_A" "$ROUTE_A" "$HTTP_A"
        API_A_PID="$LAST_PID"
        ;;
      api-b)
        start_provider api-b "$API_B" "$ROUTE_B" "$HTTP_B"
        API_B_PID="$LAST_PID"
        ;;
    esac
  done
}

start_workflow_provider() {
  local rid="$1"
  local workflow="$2"
  ZLINK_CPP_E2E_PROVIDER_RID="$rid" \
  ZLINK_CPP_E2E_PROVIDER_INSTANCE="$rid" \
  ZLINK_CPP_E2E_WORKFLOW_ENDPOINT="$workflow" \
  ZLINK_CPP_E2E_HTTP_ENDPOINT="$HTTP_WORKFLOW" \
  ZLINK_CPP_E2E_REDIS_ENDPOINT="$REDIS_ENDPOINT" \
  ZLINK_CPP_E2E_REDIS_KEY_PREFIX="$REDIS_KEY_PREFIX" \
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
  local redis_endpoint="$4"
  shift 4
  env "$@" \
  ZLINK_CPP_E2E_HTTP_ENDPOINT="$http" \
  ZLINK_CPP_E2E_PROVIDER_ENDPOINTS="$endpoints" \
  ZLINK_CPP_E2E_REDIS_ENDPOINT="$redis_endpoint" \
  ZLINK_CPP_E2E_REDIS_KEY_PREFIX="$REDIS_KEY_PREFIX" \
  ZLINK_CPP_E2E_TRACE_LABEL="$name" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$CONSUMER_SERVER" >"$LOG_DIR/$name.stdout.log" 2>"$LOG_DIR/$name.stderr.log" &
  LAST_PID="$!"
  PIDS+=("$LAST_PID")
  wait_port "$name-http" "$http"
}

stop_pid() {
  local pid="$1"
  if [[ -z "$pid" ]]; then
    return 0
  fi
  if kill -0 "$pid" >/dev/null 2>&1; then
    kill "$pid" >/dev/null 2>&1 || true
    wait_pid_status "$pid" "stopped process $pid"
  fi
}

wait_marker() {
  local file="$1"
  for _ in $(seq 1 "$LOCAL_READINESS_ATTEMPTS"); do
    if [[ -f "$file" ]]; then
      return 0
    fi
    sleep "$LOCAL_READINESS_POLL_SECONDS"
  done
  echo "Timed out waiting ${LOCAL_READINESS_TIMEOUT_SECONDS}s for marker $file" >&2
  return 1
}

wait_consumer_profile_ready() {
  local name="$1"
  local consumer_url="$2"
  local marker="$3"
  local output="$LOG_DIR/${name}.ready.log"
  local body
  body="{\"value\":\"$marker\"}"
  for _ in $(seq 1 "$LOCAL_READINESS_ATTEMPTS"); do
    if curl -fsS \
      -H 'Content-Type: application/json' \
      --max-time 2 \
      -d "$body" \
      "$consumer_url/profile/request" >"$output" 2>&1; then
      if grep -Fq "profile:$marker" "$output"; then
        return 0
      fi
    fi
    sleep "$LOCAL_READINESS_POLL_SECONDS"
  done
  echo "Timed out waiting ${LOCAL_READINESS_TIMEOUT_SECONDS}s for $name channel readiness through $consumer_url" >&2
  cat "$output" >&2 || true
  return 1
}

run_client() {
  local scenario="$1"
  local suffix="$2"
  shift 2
  ZLINK_CPP_E2E_SCENARIO="$scenario" \
  ZLINK_CPP_E2E_API_A_ENDPOINT="$API_A" \
  ZLINK_CPP_E2E_API_A2_ENDPOINT="$API_A2" \
  ZLINK_CPP_E2E_API_B_ENDPOINT="$API_B" \
  ZLINK_CPP_E2E_ROUTE_A_ENDPOINT="$ROUTE_A" \
  ZLINK_CPP_E2E_ROUTE_B_ENDPOINT="$ROUTE_B" \
  ZLINK_CPP_E2E_HTTP_A_ENDPOINT="$HTTP_A" \
  ZLINK_CPP_E2E_HTTP_B_ENDPOINT="$HTTP_B" \
  ZLINK_CPP_E2E_HTTP_A2_ENDPOINT="$HTTP_A2" \
  ZLINK_CPP_E2E_HTTP_WORKFLOW_ENDPOINT="$HTTP_WORKFLOW" \
  ZLINK_CPP_E2E_DIRECT_CONSUMER_URL="$HTTP_DIRECT_CONSUMER" \
  ZLINK_CPP_E2E_SINGLE_CONSUMER_URL="$HTTP_SINGLE_CONSUMER" \
  ZLINK_CPP_E2E_STORE_CONSUMER_URL="$HTTP_STORE_CONSUMER" \
  ZLINK_CPP_E2E_BACKPRESSURE_CONSUMER_URL="$HTTP_BACKPRESSURE_CONSUMER" \
  ZLINK_CPP_E2E_CLIENT_ROUTE_ENDPOINT="$CLIENT_ROUTE" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
  "$@" \
    "$CLIENT" >"$LOG_DIR/client-$suffix.stdout.log" 2>"$LOG_DIR/client-$suffix.stderr.log"
}

if [[ "$SCENARIO" == "all" ]]; then
  CHILD_LOG_MANIFEST="$LOG_DIR/child-runs.log"
  for scenario in RM-A1 RM-A2 RM-A4 RM-A6 RM-B1 RM-B2 RM-C1 RM-C2 RM-C3 RM-C4 RM-C5 RM-C7 RM-C8 RM-C9; do
    echo "running $scenario"
    child_output="$LOG_DIR/child-$scenario.output.log"
    ZLINK_CPP_E2E_BUILD_DIR="$BUILD_DIR" \
      ZLINK_REDIS_E2E_ENDPOINT="$REDIS_ENDPOINT" \
      ZLINK_CPP_E2E_OWNED_REDIS_CONTAINER="$REDIS_CONTAINER" \
      "$0" "$scenario" | tee "$child_output"
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

if [[ "$SCENARIO" == "RM-A2" || "$SCENARIO" == "rm-a2" ]]; then
  start_provider api-a "$API_A" "$ROUTE_A" "$HTTP_A"
  API_A_PID="$LAST_PID"
  sleep "$ROUTE_SETTLE_SECONDS"
  run_client rm-a2 rm-a2 env
  cat "$LOG_DIR/client-rm-a2.stdout.log"
  exit 0
fi

if [[ "$SCENARIO" == "RM-A1" || "$SCENARIO" == "rm-a1" ]]; then
  start_standard_provider_pair
  sleep "$ROUTE_SETTLE_SECONDS"
  run_client rm-a1 rm-a1 env
  cat "$LOG_DIR/client-rm-a1.stdout.log"
  exit 0
fi

if [[ "$SCENARIO" == "RM-A4" || "$SCENARIO" == "rm-a4" ]]; then
  if ! rg -q 'ZLINK_CPP_E2E_STORE_CONSUMER_URL' \
      "$SCRIPT_DIR/Client/Scenarios/rm_a4_same_rid_failover_scenario.hpp" \
    || ! rg -q 'locations/peers' \
      "$SCRIPT_DIR/Client/Scenarios/rm_a4_same_rid_failover_scenario.hpp"; then
    echo "RM-A4 contract gate failed: persistent consumer handover assertions are missing" >&2
    exit 1
  fi
  start_provider api-a "$API_A" "$ROUTE_A" "$HTTP_A" api-a-v1
  API_A_PID="$LAST_PID"
  start_consumer store-consumer "$HTTP_STORE_CONSUMER" "" "$REDIS_ENDPOINT"
  STORE_CONSUMER_PID="$LAST_PID"
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
  sleep "$ROUTE_SETTLE_SECONDS"
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
  sleep "$ROUTE_SETTLE_SECONDS"
  touch "$CONTINUE"
  wait "$B1_CLIENT_PID"
  cat "$LOG_DIR/client-rm-b1.stdout.log"
  exit 0
fi

if [[ "$SCENARIO" == "RM-B2" || "$SCENARIO" == "rm-b2" ]]; then
  start_standard_provider_pair
  READY="$LOG_DIR/rm-b2-ready"
  CONTINUE="$LOG_DIR/rm-b2-continue"
  run_client rm-b2 rm-b2 env \
    ZLINK_CPP_E2E_READY_FILE="$READY" \
    ZLINK_CPP_E2E_CONTINUE_FILE="$CONTINUE" &
  B2_CLIENT_PID="$!"
  wait_marker "$READY"
  stop_pid "$API_B_PID"
  sleep "$ROUTE_SETTLE_SECONDS"
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
  sleep "$ROUTE_SETTLE_SECONDS"
  run_client rm-c1 rm-c1 env
  cat "$LOG_DIR/client-rm-c1.stdout.log"
  exit 0
fi

if [[ "$SCENARIO" == "RM-C2" || "$SCENARIO" == "rm-c2" ]]; then
  start_provider api-a "$API_A" "$ROUTE_A" "$HTTP_A"
  API_A_PID="$LAST_PID"
  start_provider api-b "$API_B" "$ROUTE_B" "$HTTP_B"
  API_B_PID="$LAST_PID"
  sleep "$ROUTE_SETTLE_SECONDS"
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
  sleep "$ROUTE_SETTLE_SECONDS"
  run_client rm-c3 rm-c3 env
  cat "$LOG_DIR/client-rm-c3.stdout.log"
  exit 0
fi

if [[ "$SCENARIO" == "RM-C4" || "$SCENARIO" == "rm-c4" ]]; then
  start_provider api-a "$API_A" "$ROUTE_A" "$HTTP_A"
  API_A_PID="$LAST_PID"
  start_provider api-b "$API_B" "$ROUTE_B" "$HTTP_B"
  API_B_PID="$LAST_PID"
  start_consumer store-consumer "$HTTP_STORE_CONSUMER" "" "$REDIS_ENDPOINT"
  STORE_CONSUMER_PID="$LAST_PID"
  sleep "$ROUTE_SETTLE_SECONDS"
  run_client rm-c4 rm-c4 env
  cat "$LOG_DIR/client-rm-c4.stdout.log"
  exit 0
fi

if [[ "$SCENARIO" == "RM-C5" || "$SCENARIO" == "rm-c5" ]]; then
  start_provider api-a "$API_A" "$ROUTE_A" "$HTTP_A"
  API_A_PID="$LAST_PID"
  start_provider api-b "$API_B" "$ROUTE_B" "$HTTP_B"
  API_B_PID="$LAST_PID"
  start_consumer store-consumer "$HTTP_STORE_CONSUMER" "" "$REDIS_ENDPOINT"
  STORE_CONSUMER_PID="$LAST_PID"
  sleep "$ROUTE_SETTLE_SECONDS"
  run_client rm-c5 rm-c5 env
  cat "$LOG_DIR/client-rm-c5.stdout.log"
  exit 0
fi

if [[ "$SCENARIO" == "RM-C7" || "$SCENARIO" == "rm-c7" ]]; then
  start_provider api-a "$API_A" "$ROUTE_A" "$HTTP_A" api-a ZLINK_CPP_E2E_SERVER_WEIGHT=75
  API_A_PID="$LAST_PID"
  start_provider api-b "$API_B" "$ROUTE_B" "$HTTP_B" api-b ZLINK_CPP_E2E_SERVER_WEIGHT=25
  API_B_PID="$LAST_PID"
  start_consumer weighted-consumer "$HTTP_SINGLE_CONSUMER" "" "$REDIS_ENDPOINT"
  WEIGHTED_CONSUMER_PID="$LAST_PID"
  sleep "$ROUTE_SETTLE_SECONDS"
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
  sleep "$ROUTE_SETTLE_SECONDS"
  run_client rm-c8 rm-c8 env
  cat "$LOG_DIR/client-rm-c8.stdout.log"
  stop_pid "$SINGLE_CONSUMER_PID"
  stop_pid "$API_A_PID"
  stop_pid "$API_B_PID"

  start_provider api-a "$API_A2" "$ROUTE_A2" "$HTTP_A2" api-a ZLINK_CPP_E2E_MAX_MESSAGE_SIZE=2097152
  API_A_PID="$LAST_PID"
  start_consumer single-consumer-max "$HTTP_STORE_CONSUMER" "$API_A2" "" \
    ZLINK_CPP_E2E_CLIENT_MAX_MESSAGE_SIZE=2097152
  SINGLE_CONSUMER_PID="$LAST_PID"
  wait_consumer_profile_ready single-consumer-max "$HTTP_STORE_CONSUMER" "rm-c8-max-ready"
  run_client rm-c8-max rm-c8-max env ZLINK_CPP_E2E_SINGLE_CONSUMER_URL="$HTTP_STORE_CONSUMER"
  cat "$LOG_DIR/client-rm-c8-max.stdout.log"
  exit 0
fi

if [[ "$SCENARIO" == "RM-C9" || "$SCENARIO" == "rm-c9" ]]; then
  start_provider api-a "$API_A" "$ROUTE_A" "$HTTP_A"
  API_A_PID="$LAST_PID"
  start_consumer backpressure-consumer "$HTTP_BACKPRESSURE_CONSUMER" "$API_A" ""
  BACKPRESSURE_CONSUMER_PID="$LAST_PID"
  sleep "$ROUTE_SETTLE_SECONDS"
  run_client rm-c9 rm-c9 env
  cat "$LOG_DIR/client-rm-c9.stdout.log"
  exit 0
fi

echo "Unknown RegistryMessaging scenario: $SCENARIO" >&2
exit 1
