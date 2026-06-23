#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${ZLINK_CPP_E2E_BUILD_DIR:-$CPP_DIR/build}"

read -r REGISTRY_PUB REGISTRY_ROUTER PUBLISHER HTTP_1 HTTP_2 HTTP_3 <<<"$(python3 - <<'PY'
import socket

sockets = []
ports = []
for _ in range(6):
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    sockets.append(s)
    ports.append(s.getsockname()[1])
print(" ".join(f"tcp://127.0.0.1:{p}" for p in ports[:3]), end=" ")
print(" ".join(f"http://127.0.0.1:{p}" for p in ports[3:6]))
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
  zlink_cpp_e2e_pubsub_server \
  zlink_cpp_e2e_pubsub_client >/dev/null

SERVER="$BUILD_DIR/zlink_cpp_e2e_pubsub_server"
CLIENT="$BUILD_DIR/zlink_cpp_e2e_pubsub_client"
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

start_registry() {
  ZLINK_CPP_E2E_ROLE=registry \
  ZLINK_CPP_E2E_REGISTRY_PUB="$REGISTRY_PUB" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$SERVER" >"$LOG_DIR/registry.stdout.log" 2>"$LOG_DIR/registry.stderr.log" &
  PIDS+=("$!")
  wait_port registry-router "$REGISTRY_ROUTER"
}

start_subscriber() {
  local id="$1"
  local topics="$2"
  local http="$3"
  ZLINK_CPP_E2E_ROLE=subscriber \
  ZLINK_CPP_E2E_SUBSCRIBER_ID="$id" \
  ZLINK_CPP_E2E_TOPICS="$topics" \
  ZLINK_CPP_E2E_HTTP_ENDPOINT="$http" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$SERVER" >"$LOG_DIR/$id.stdout.log" 2>"$LOG_DIR/$id.stderr.log" &
  LAST_PID="$!"
  PIDS+=("$LAST_PID")
  wait_port "$id-http" "$http"
}

stop_all_subscribers() {
  for pid in "${SUB_PIDS[@]:-}"; do
    if kill -0 "$pid" >/dev/null 2>&1; then
      kill "$pid" >/dev/null 2>&1 || true
      wait "$pid" >/dev/null 2>&1 || true
    fi
  done
  SUB_PIDS=()
}

run_client() {
  local scenario="$1"
  local suffix="$2"
  shift 2
  ZLINK_CPP_E2E_SCENARIO="$scenario" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
  "$@" \
    "$CLIENT" >"$LOG_DIR/client-$suffix.stdout.log" 2>"$LOG_DIR/client-$suffix.stderr.log"
}

start_client_waiting() {
  local scenario="$1"
  local suffix="$2"
  local ready="$3"
  local continue_file="$4"
  shift 4
  run_client "$scenario" "$suffix" env \
    ZLINK_CPP_E2E_START_READY_FILE="$ready" \
    ZLINK_CPP_E2E_START_CONTINUE_FILE="$continue_file" \
    "$@" &
  LAST_PID="$!"
}

verify() {
  local mode="$1"
  shift
  python3 - "$mode" "$@" <<'PY'
import json
import sys
import time
import urllib.request

mode = sys.argv[1]
endpoints = sys.argv[2:]

def load(endpoint):
    with urllib.request.urlopen(endpoint + "/evidence", timeout=2) as response:
        return json.loads(response.read().decode())

def wait_for(predicate, message):
    last = None
    for _ in range(100):
        snapshots = [load(endpoint) for endpoint in endpoints]
        last = snapshots
        if predicate(snapshots):
            return
        time.sleep(0.1)
    raise SystemExit(message + "\nlast=" + json.dumps(last, sort_keys=True))

def values(snapshot, topic=None):
    items = snapshot["events"]
    if topic is not None:
        items = [event for event in items if event["topic"] == topic]
    return [event["value"] for event in items]

if mode == "basic":
    expected = [f"measure-{index}" for index in range(20)]
    wait_for(lambda ss: all(all(value in values(s, "fanout") for value in expected) for s in ss),
             "PS-A1 evidence check failed")
elif mode == "topic":
    def ok(ss):
        by_id = {s["subscriber_id"]: s for s in ss}
        sub1 = values(by_id["sub-1"], "alpha")
        sub2 = values(by_id["sub-2"], "beta")
        sub3 = values(by_id["sub-3"], "alpha")
        return all(f"alpha-{i}" in sub1 for i in range(8)) and \
            all(f"beta-{i}" in sub2 for i in range(8)) and \
            all(f"alpha-{i}" in sub3 for i in range(8)) and \
            not values(by_id["sub-1"], "beta") and \
            not values(by_id["sub-2"], "alpha") and \
            not values(by_id["sub-3"], "beta")
    wait_for(ok, "PS-A2 evidence check failed")
elif mode == "late":
    def ok(ss):
        by_id = {s["subscriber_id"]: s for s in ss}
        late = values(by_id["sub-3"], "fanout")
        early_ok = all(f"before-late-{i}" in values(by_id["sub-1"], "fanout") for i in range(5)) and \
            all(f"before-late-{i}" in values(by_id["sub-2"], "fanout") for i in range(5))
        late_ok = all(f"after-late-{i}" in late for i in range(8)) and \
            not any(value.startswith("before-late-") for value in late)
        return early_ok and late_ok
    wait_for(ok, "PS-A3 evidence check failed")
elif mode == "negative":
    def ok(ss):
        for snapshot in ss:
            if "after-missing" not in values(snapshot, "fanout"):
                return False
            found = [error for error in snapshot["errors"]
                     if error["message_kind"] == "publish"
                     and error["reason"] == "handlerMissing"
                     and error["action"] == "drop"
                     and error["packet_name"] == "MissingEventNotify"
                     and error["topic"] == "fanout"]
            if not found:
                return False
        return True
    wait_for(ok, "PS-C1 evidence check failed")
else:
    raise SystemExit("unknown verify mode " + mode)
PY
}

start_registry

SUB_PIDS=()
START_READY="$LOG_DIR/ps-a1-start-ready"
START_CONTINUE="$LOG_DIR/ps-a1-start-continue"
start_client_waiting basic basic "$START_READY" "$START_CONTINUE"
BASIC_CLIENT_PID="$LAST_PID"
wait_marker "$START_READY"
start_subscriber sub-1 fanout "$HTTP_1"; SUB_PIDS+=("$LAST_PID")
start_subscriber sub-2 fanout "$HTTP_2"; SUB_PIDS+=("$LAST_PID")
start_subscriber sub-3 fanout "$HTTP_3"; SUB_PIDS+=("$LAST_PID")
sleep 1
touch "$START_CONTINUE"
wait "$BASIC_CLIENT_PID"
cat "$LOG_DIR/client-basic.stdout.log"
verify basic "$HTTP_1" "$HTTP_2" "$HTTP_3"
stop_all_subscribers

START_READY="$LOG_DIR/ps-a2-start-ready"
START_CONTINUE="$LOG_DIR/ps-a2-start-continue"
start_client_waiting topic topic "$START_READY" "$START_CONTINUE"
TOPIC_CLIENT_PID="$LAST_PID"
wait_marker "$START_READY"
start_subscriber sub-1 fanout,alpha "$HTTP_1"; SUB_PIDS+=("$LAST_PID")
start_subscriber sub-2 fanout,beta "$HTTP_2"; SUB_PIDS+=("$LAST_PID")
start_subscriber sub-3 fanout,alpha "$HTTP_3"; SUB_PIDS+=("$LAST_PID")
sleep 1
touch "$START_CONTINUE"
wait "$TOPIC_CLIENT_PID"
cat "$LOG_DIR/client-topic.stdout.log"
verify topic "$HTTP_1" "$HTTP_2" "$HTTP_3"
stop_all_subscribers

START_READY="$LOG_DIR/ps-a3-start-ready"
START_CONTINUE="$LOG_DIR/ps-a3-start-continue"
READY="$LOG_DIR/ps-a3-ready"
CONTINUE="$LOG_DIR/ps-a3-continue"
start_client_waiting late late "$START_READY" "$START_CONTINUE" \
  ZLINK_CPP_E2E_READY_FILE="$READY" \
  ZLINK_CPP_E2E_CONTINUE_FILE="$CONTINUE"
LATE_CLIENT_PID="$LAST_PID"
wait_marker "$START_READY"
start_subscriber sub-1 fanout "$HTTP_1"; SUB_PIDS+=("$LAST_PID")
start_subscriber sub-2 fanout "$HTTP_2"; SUB_PIDS+=("$LAST_PID")
sleep 1
touch "$START_CONTINUE"
wait_marker "$READY"
start_subscriber sub-3 fanout "$HTTP_3"; SUB_PIDS+=("$LAST_PID")
sleep 1
touch "$CONTINUE"
wait "$LATE_CLIENT_PID"
cat "$LOG_DIR/client-late.stdout.log"
verify late "$HTTP_1" "$HTTP_2" "$HTTP_3"
stop_all_subscribers

START_READY="$LOG_DIR/ps-c1-start-ready"
START_CONTINUE="$LOG_DIR/ps-c1-start-continue"
start_client_waiting negative negative "$START_READY" "$START_CONTINUE"
NEGATIVE_CLIENT_PID="$LAST_PID"
wait_marker "$START_READY"
start_subscriber sub-1 fanout "$HTTP_1"; SUB_PIDS+=("$LAST_PID")
start_subscriber sub-2 fanout "$HTTP_2"; SUB_PIDS+=("$LAST_PID")
start_subscriber sub-3 fanout "$HTTP_3"; SUB_PIDS+=("$LAST_PID")
sleep 1
touch "$START_CONTINUE"
wait "$NEGATIVE_CLIENT_PID"
cat "$LOG_DIR/client-negative.stdout.log"
verify negative "$HTTP_1" "$HTTP_2" "$HTTP_3"
stop_all_subscribers
