#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${ZLINK_CPP_E2E_BUILD_DIR:-$CPP_DIR/build}"

read -r REGISTRY_PUB REGISTRY_ROUTER PUBLISHER PUBLISHER_HTTP HTTP_1 HTTP_2 HTTP_3 <<<"$(python3 - <<'PY'
import socket

sockets = []
ports = []
for _ in range(7):
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    sockets.append(s)
    ports.append(s.getsockname()[1])
print(" ".join(f"tcp://127.0.0.1:{p}" for p in ports[:3]), end=" ")
print(" ".join(f"http://127.0.0.1:{p}" for p in ports[3:7]))
for s in sockets:
    s.close()
PY
)"

RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$SCRIPT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"
echo "log_dir=$LOG_DIR"
SCENARIO="${1:-all}"

cmake -S "$CPP_DIR" -B "$BUILD_DIR" >/dev/null
cmake --build "$BUILD_DIR" --target \
  zlink_cpp_e2e_pubsub_registry \
  zlink_cpp_e2e_pubsub_publisher \
  zlink_cpp_e2e_pubsub_subscriber \
  zlink_cpp_e2e_pubsub_client >/dev/null

REGISTRY="$BUILD_DIR/zlink_cpp_e2e_pubsub_registry"
PUBLISHER_SERVER="$BUILD_DIR/zlink_cpp_e2e_pubsub_publisher"
SUBSCRIBER="$BUILD_DIR/zlink_cpp_e2e_pubsub_subscriber"
CLIENT="$BUILD_DIR/zlink_cpp_e2e_pubsub_client"
PIDS=()
LAST_PID=""
PUBLISHER_PID=""

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

wait_port_closed() {
  local name="$1"
  local endpoint="$2"
  local host="127.0.0.1"
  local port
  port="$(port_of "$endpoint")"
  for _ in $(seq 1 100); do
    if ! (echo >"/dev/tcp/${host}/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.05
  done
  echo "Timed out waiting for $name to close at $endpoint" >&2
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
  ZLINK_CPP_E2E_REGISTRY_PUB="$REGISTRY_PUB" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$REGISTRY" >"$LOG_DIR/registry.stdout.log" 2>"$LOG_DIR/registry.stderr.log" &
  PIDS+=("$!")
  wait_port registry-router "$REGISTRY_ROUTER"
}

start_publisher() {
  local suffix="${1:-publisher}"
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER" \
  ZLINK_CPP_E2E_PUBLISHER_HTTP_ENDPOINT="$PUBLISHER_HTTP" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$PUBLISHER_SERVER" >"$LOG_DIR/$suffix.stdout.log" 2>"$LOG_DIR/$suffix.stderr.log" &
  LAST_PID="$!"
  PUBLISHER_PID="$LAST_PID"
  PIDS+=("$LAST_PID")
  wait_port "$suffix-http" "$PUBLISHER_HTTP"
}

start_subscriber() {
  local id="$1"
  local topics="$2"
  local http="$3"
  local delay="${4:-0}"
  local accepted_topics="${5:-$topics}"
  ZLINK_CPP_E2E_SUBSCRIBER_ID="$id" \
  ZLINK_CPP_E2E_TOPICS="$topics" \
  ZLINK_CPP_E2E_ACCEPTED_TOPICS="$accepted_topics" \
  ZLINK_CPP_E2E_HANDLER_DELAY_MS="$delay" \
  ZLINK_CPP_E2E_HTTP_ENDPOINT="$http" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$SUBSCRIBER" >"$LOG_DIR/$id.stdout.log" 2>"$LOG_DIR/$id.stderr.log" &
  LAST_PID="$!"
  PIDS+=("$LAST_PID")
  wait_port "$id-http" "$http"
}

stop_pid() {
  local pid="$1"
  if kill -0 "$pid" >/dev/null 2>&1; then
    kill "$pid" >/dev/null 2>&1 || true
    wait "$pid" >/dev/null 2>&1 || true
  fi
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

should_run() {
  [[ "$SCENARIO" == "all" || "$SCENARIO" == "$1" || "$SCENARIO" == "$2" ]]
}

run_client() {
  local scenario="$1"
  local suffix="$2"
  shift 2
  ZLINK_CPP_E2E_SCENARIO="$scenario" \
  ZLINK_CPP_E2E_PUBLISHER_URL="$PUBLISHER_HTTP" \
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
import urllib.request

mode = sys.argv[1]
endpoints = sys.argv[2:]

def post_json(endpoint, path, payload):
    request = urllib.request.Request(
        endpoint + path,
        data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json"},
        method="POST")
    with urllib.request.urlopen(request, timeout=35) as response:
        return json.loads(response.read().decode())

def wait_lines(endpoint, *, contains_all=None, contains_any_groups=None,
               contains_all_line_groups=None, contains_any_line_groups=None,
               timeout_ms=10000):
    return post_json(endpoint, "/evidence/wait", {
        "contains_all": contains_all or [],
        "contains_any_groups": contains_any_groups or [],
        "contains_all_line_groups": contains_all_line_groups or [],
        "contains_any_line_groups": contains_any_line_groups or [],
        "timeout_milliseconds": timeout_ms,
    })

def accepted(value, topic="fanout"):
    return ["accepted|", f"topic={topic}", f"value={value}"]

def ignored(value, topic):
    return ["ignored|", f"topic={topic}", f"value={value}"]

def error_line(packet, topic="fanout"):
    return ["error|", "kind=publish", "reason=handlerMissing", "action=drop",
            f"packet={packet}", f"topic={topic}"]

def has_line(lines, *parts):
    return any(all(part in line for part in parts) for line in lines)

def assert_no_line(lines, message, *parts):
    if has_line(lines, *parts):
        raise SystemExit(message + "\nlines=" + json.dumps(lines, sort_keys=True))

if mode == "basic":
    groups = [accepted(f"measure-{index}") for index in range(20)]
    for endpoint in endpoints:
        wait_lines(endpoint, contains_all_line_groups=groups)
elif mode == "topic":
    sub1 = wait_lines(endpoints[0],
                      contains_all_line_groups=[accepted(f"alpha-{i}", "alpha") for i in range(8)]
                      + [ignored(f"beta-{i}", "beta") for i in range(8)])
    sub2 = wait_lines(endpoints[1],
                      contains_all_line_groups=[accepted(f"beta-{i}", "beta") for i in range(8)]
                      + [ignored(f"alpha-{i}", "alpha") for i in range(8)])
    sub3 = wait_lines(endpoints[2],
                      contains_all_line_groups=[accepted(f"alpha-{i}", "alpha") for i in range(8)]
                      + [ignored(f"beta-{i}", "beta") for i in range(8)])
    assert_no_line(sub1, "PS-A2 sub-1 accepted beta unexpectedly", "accepted|", "topic=beta")
    assert_no_line(sub2, "PS-A2 sub-2 accepted alpha unexpectedly", "accepted|", "topic=alpha")
    assert_no_line(sub3, "PS-A2 sub-3 accepted beta unexpectedly", "accepted|", "topic=beta")
elif mode == "late":
    early_groups = [accepted(f"before-late-{i}") for i in range(5)] \
        + [accepted(f"after-late-{i}") for i in range(8)]
    wait_lines(endpoints[0], contains_all_line_groups=early_groups)
    wait_lines(endpoints[1], contains_all_line_groups=early_groups)
    late = wait_lines(endpoints[2],
                      contains_all_line_groups=[accepted(f"after-late-{i}") for i in range(8)])
    assert_no_line(late, "PS-A3 late subscriber received pre-join event", "accepted|",
                   "value=before-late-")
elif mode == "reconnect":
    stable_groups = [accepted(f"during-reconnect-{i}") for i in range(5)] \
        + [accepted(f"after-reconnect-{i}") for i in range(8)]
    wait_lines(endpoints[0], contains_all_line_groups=stable_groups)
    wait_lines(endpoints[1], contains_all_line_groups=stable_groups)
    rejoined = wait_lines(endpoints[2],
                          contains_all_line_groups=[accepted(f"after-reconnect-{i}")
                                                    for i in range(8)])
    assert_no_line(rejoined, "PS-A4 rejoined subscriber received disconnect-gap event",
                   "accepted|", "value=during-reconnect-")
elif mode == "slow":
    groups = [accepted(f"slow-isolation-{i}") for i in range(16)]
    wait_lines(endpoints[1], contains_all_line_groups=groups)
    wait_lines(endpoints[2], contains_all_line_groups=groups)
elif mode == "publisher-restart":
    groups = [accepted(f"after-publisher-restart-{i}") for i in range(20, 43)]
    for endpoint in endpoints:
        wait_lines(endpoint, contains_all_line_groups=groups)
elif mode == "negative":
    groups = [accepted("after-missing"), error_line("MissingEventMsg")]
    for endpoint in endpoints:
        wait_lines(endpoint, contains_all_line_groups=groups)
else:
    raise SystemExit("unknown verify mode " + mode)
PY
}

case "$SCENARIO" in
  all|PS-A1|ps-a1|PS-A2|ps-a2|PS-A3|ps-a3|PS-A4|ps-a4|PS-B1|ps-b1|PS-B2|ps-b2|PS-C1|ps-c1)
    ;;
  *)
    echo "Unknown PubSub scenario: $SCENARIO" >&2
    exit 1
    ;;
esac

start_registry
start_publisher publisher

SUB_PIDS=()
if should_run PS-A1 ps-a1; then
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
fi

if should_run PS-A2 ps-a2; then
  START_READY="$LOG_DIR/ps-a2-start-ready"
  START_CONTINUE="$LOG_DIR/ps-a2-start-continue"
  start_client_waiting topic topic "$START_READY" "$START_CONTINUE"
  TOPIC_CLIENT_PID="$LAST_PID"
  wait_marker "$START_READY"
  start_subscriber sub-1 alpha,beta "$HTTP_1" 0 alpha; SUB_PIDS+=("$LAST_PID")
  start_subscriber sub-2 alpha,beta "$HTTP_2" 0 beta; SUB_PIDS+=("$LAST_PID")
  start_subscriber sub-3 alpha,beta "$HTTP_3" 0 alpha; SUB_PIDS+=("$LAST_PID")
  sleep 1
  touch "$START_CONTINUE"
  wait "$TOPIC_CLIENT_PID"
  cat "$LOG_DIR/client-topic.stdout.log"
  verify topic "$HTTP_1" "$HTTP_2" "$HTTP_3"
  stop_all_subscribers
fi

if should_run PS-A3 ps-a3; then
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
fi

if should_run PS-A4 ps-a4; then
  START_READY="$LOG_DIR/ps-a4-start-ready"
  START_CONTINUE="$LOG_DIR/ps-a4-start-continue"
  READY="$LOG_DIR/ps-a4-ready"
  CONTINUE="$LOG_DIR/ps-a4-continue"
  RESTART_READY="$LOG_DIR/ps-a4-restart-ready"
  RESTART_CONTINUE="$LOG_DIR/ps-a4-restart-continue"
  start_client_waiting reconnect reconnect "$START_READY" "$START_CONTINUE" \
    ZLINK_CPP_E2E_READY_FILE="$READY" \
    ZLINK_CPP_E2E_CONTINUE_FILE="$CONTINUE" \
    ZLINK_CPP_E2E_RESTART_READY_FILE="$RESTART_READY" \
    ZLINK_CPP_E2E_RESTART_CONTINUE_FILE="$RESTART_CONTINUE"
  RECONNECT_CLIENT_PID="$LAST_PID"
  wait_marker "$START_READY"
  start_subscriber sub-1 fanout "$HTTP_1"; SUB_PIDS+=("$LAST_PID")
  start_subscriber sub-2 fanout "$HTTP_2"; SUB_PIDS+=("$LAST_PID")
  start_subscriber sub-3 fanout "$HTTP_3"; SUB3_PID="$LAST_PID"; SUB_PIDS+=("$LAST_PID")
  sleep 1
  touch "$START_CONTINUE"
  wait_marker "$READY"
  stop_pid "$SUB3_PID"
  wait_port_closed sub-3-http "$HTTP_3"
  touch "$CONTINUE"
  wait_marker "$RESTART_READY"
  start_subscriber sub-3 fanout "$HTTP_3"; SUB3_PID="$LAST_PID"; SUB_PIDS+=("$LAST_PID")
  sleep 1
  touch "$RESTART_CONTINUE"
  wait "$RECONNECT_CLIENT_PID"
  cat "$LOG_DIR/client-reconnect.stdout.log"
  verify reconnect "$HTTP_1" "$HTTP_2" "$HTTP_3"
  stop_all_subscribers
fi

if should_run PS-B1 ps-b1; then
  START_READY="$LOG_DIR/ps-b1-start-ready"
  START_CONTINUE="$LOG_DIR/ps-b1-start-continue"
  start_client_waiting slow slow "$START_READY" "$START_CONTINUE"
  SLOW_CLIENT_PID="$LAST_PID"
  wait_marker "$START_READY"
  start_subscriber sub-1 fanout "$HTTP_1" 250; SUB_PIDS+=("$LAST_PID")
  start_subscriber sub-2 fanout "$HTTP_2"; SUB_PIDS+=("$LAST_PID")
  start_subscriber sub-3 fanout "$HTTP_3"; SUB_PIDS+=("$LAST_PID")
  sleep 1
  touch "$START_CONTINUE"
  wait "$SLOW_CLIENT_PID"
  cat "$LOG_DIR/client-slow.stdout.log"
  verify slow "$HTTP_1" "$HTTP_2" "$HTTP_3"
  stop_all_subscribers
fi

if should_run PS-B2 ps-b2; then
  START_READY="$LOG_DIR/ps-b2-start-ready"
  START_CONTINUE="$LOG_DIR/ps-b2-start-continue"
  start_subscriber sub-1 fanout "$HTTP_1"; SUB_PIDS+=("$LAST_PID")
  start_subscriber sub-2 fanout "$HTTP_2"; SUB_PIDS+=("$LAST_PID")
  start_subscriber sub-3 fanout "$HTTP_3"; SUB_PIDS+=("$LAST_PID")
  start_client_waiting publisher-restart publisher-before "$START_READY" "$START_CONTINUE" \
    ZLINK_CPP_E2E_PUBLISHER_RESTART_PHASE=before
  PUB_RESTART_CLIENT_PID="$LAST_PID"
  wait_marker "$START_READY"
  sleep 1
  touch "$START_CONTINUE"
  wait "$PUB_RESTART_CLIENT_PID"
  cat "$LOG_DIR/client-publisher-before.stdout.log"
  sleep 1
  stop_pid "$PUBLISHER_PID"
  wait_port_closed publisher-http "$PUBLISHER_HTTP"
  start_publisher publisher-restart
  START_READY="$LOG_DIR/ps-b2-restart-ready"
  START_CONTINUE="$LOG_DIR/ps-b2-restart-continue"
  start_client_waiting publisher-restart publisher-after "$START_READY" "$START_CONTINUE" \
    ZLINK_CPP_E2E_PUBLISHER_RESTART_PHASE=after
  PUB_RESTART_CLIENT_PID="$LAST_PID"
  wait_marker "$START_READY"
  sleep 1
  touch "$START_CONTINUE"
  wait "$PUB_RESTART_CLIENT_PID"
  cat "$LOG_DIR/client-publisher-after.stdout.log"
  verify publisher-restart "$HTTP_1" "$HTTP_2" "$HTTP_3"
  stop_all_subscribers
fi

if should_run PS-C1 ps-c1; then
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
fi

echo "pubsub e2e result=passed"
