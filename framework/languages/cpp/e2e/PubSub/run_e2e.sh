#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
source "$SCRIPT_DIR/../redis-common.sh"
BUILD_DIR="${ZLINK_CPP_E2E_BUILD_DIR:-$CPP_DIR/build}"
LOCAL_READINESS_TIMEOUT_SECONDS=3
LOCAL_READINESS_POLL_SECONDS=0.1
ROUTE_SETTLE_SECONDS=5
SCENARIO_SETTLE_SECONDS=3
HTTP_PROBE_TIMEOUT_SECONDS=3
BOUNDED_EVIDENCE_WAIT_HTTP_TIMEOUT_SECONDS=35
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

read -r PUBLISHER PUBLISHER_HTTP HTTP_1 HTTP_2 HTTP_3 <<<"$(python3 - <<'PY'
import socket

sockets = []
ports = []
for _ in range(5):
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    sockets.append(s)
    ports.append(s.getsockname()[1])
print(f"tcp://127.0.0.1:{ports[0]}", end=" ")
print(" ".join(f"http://127.0.0.1:{p}" for p in ports[1:5]))
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
  zlink_cpp_e2e_pubsub_publisher \
  zlink_cpp_e2e_pubsub_subscriber \
  zlink_cpp_e2e_pubsub_client >/dev/null

PUBLISHER_SERVER="$BUILD_DIR/zlink_cpp_e2e_pubsub_publisher"
SUBSCRIBER="$BUILD_DIR/zlink_cpp_e2e_pubsub_subscriber"
CLIENT="$BUILD_DIR/zlink_cpp_e2e_pubsub_client"
PIDS=()
LAST_PID=""
PUBLISHER_PID=""
REDIS_CONTAINER=""
REDIS_CONTAINER_OWNED=0

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

REDIS_KEY_PREFIX="zlink:e2e:cfg3:$(date +%s)-$$"
if [[ -n "${ZLINK_CPP_E2E_OWNED_REDIS_CONTAINER:-}" && -n "${ZLINK_REDIS_E2E_ENDPOINT:-}" ]]; then
  REDIS_ENDPOINT="$ZLINK_REDIS_E2E_ENDPOINT"
  REDIS_CONTAINER="$ZLINK_CPP_E2E_OWNED_REDIS_CONTAINER"
  echo "redis endpoint=$REDIS_ENDPOINT (existing owned container $REDIS_CONTAINER)"
elif [[ -n "${ZLINK_REDIS_E2E_ENDPOINT:-}" ]]; then
  echo "External Redis endpoint is not supported by the C++ PubSub e2e runner." >&2
  exit 2
else
  zlink_redis_start_scoped_assign REDIS_CONTAINER redis_port \
    "zlink-redis-cpp-e2e-pubsub" "redis:7-alpine"
  REDIS_CONTAINER_OWNED=1
  REDIS_ENDPOINT="127.0.0.1:${redis_port}"
  echo "redis endpoint=$REDIS_ENDPOINT (container $REDIS_CONTAINER)"
fi
REDIS_HOST="${REDIS_ENDPOINT%:*}"
REDIS_TCP_PORT="${REDIS_ENDPOINT##*:}"
wait_tcp "$REDIS_HOST" "$REDIS_TCP_PORT" redis
echo "redis key prefix=$REDIS_KEY_PREFIX"

cleanup() {
  local code=$?
  local cleanup_failed=0
  local status
  for pid in "${PIDS[@]:-}"; do
    if kill -0 "$pid" >/dev/null 2>&1; then
      kill "$pid" >/dev/null 2>&1 || true
    fi
  done
  for pid in "${PIDS[@]:-}"; do
    set +e
    wait "$pid" >/dev/null 2>&1
    status=$?
    set -e
    if [[ "$status" != "0" && "$status" != "127" && "$status" != "130" && "$status" != "143" ]]; then
      echo "cleanup process $pid exited unexpectedly with status $status" >&2
      cleanup_failed=1
    fi
  done
  if [[ -n "$REDIS_CONTAINER" && "$REDIS_CONTAINER_OWNED" == "1" ]]; then
    docker rm -fv "$REDIS_CONTAINER" >/dev/null 2>&1 || true
  fi
  if [[ $code -ne 0 ]]; then
    echo "E2E failed. Logs: $LOG_DIR" >&2
  elif [[ "$cleanup_failed" -ne 0 ]]; then
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

wait_port_closed() {
  local name="$1"
  local endpoint="$2"
  local host="127.0.0.1"
  local port
  port="$(port_of "$endpoint")"
  for _ in $(seq 1 "$LOCAL_READINESS_ATTEMPTS"); do
    if ! (echo >"/dev/tcp/${host}/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep "$LOCAL_READINESS_POLL_SECONDS"
  done
  echo "Timed out waiting ${LOCAL_READINESS_TIMEOUT_SECONDS}s for $name to close at $endpoint" >&2
  return 1
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

check_operational_endpoints() {
  local name="$1"
  local endpoint="$2"
  python3 - "$name" "$endpoint" "$LOG_DIR/$name-operational.log" "$HTTP_PROBE_TIMEOUT_SECONDS" <<'PY'
import json
import sys
import urllib.request

name = sys.argv[1]
endpoint = sys.argv[2]
log_path = sys.argv[3]
timeout_seconds = float(sys.argv[4])

def get(path):
    with urllib.request.urlopen(endpoint + path, timeout=timeout_seconds) as response:
        return response.read().decode()

def post(path):
    request = urllib.request.Request(endpoint + path, data=b"", method="POST")
    with urllib.request.urlopen(request, timeout=timeout_seconds) as response:
        return response.read().decode()

health = get("/health")
evidence = get("/evidence")
cleared = post("/evidence/clear")
with open(log_path, "a", encoding="utf-8") as log:
    log.write(f"{name} health={health}\n")
    log.write(f"{name} evidence={evidence}\n")
    log.write(f"{name} clear={cleared}\n")
print(f"operational {name} passed")
PY
}

snapshot_operational_evidence() {
  local name="$1"
  local endpoint="$2"
  local output="$3"
  python3 - "$name" "$endpoint" "$output" "$HTTP_PROBE_TIMEOUT_SECONDS" <<'PY'
import sys
import urllib.request

name = sys.argv[1]
endpoint = sys.argv[2]
output = sys.argv[3]
timeout_seconds = float(sys.argv[4])
with urllib.request.urlopen(endpoint + "/evidence", timeout=timeout_seconds) as response:
    body = response.read().decode()
with open(output, "w", encoding="utf-8") as file:
    file.write(body)
    file.write("\n")
print(f"snapshot {name} evidence written")
PY
}

start_publisher() {
  local suffix="${1:-publisher}"
  ZLINK_CPP_E2E_REDIS_ENDPOINT="$REDIS_ENDPOINT" \
  ZLINK_CPP_E2E_REDIS_KEY_PREFIX="$REDIS_KEY_PREFIX" \
  ZLINK_CPP_E2E_PUBLISHER_ENDPOINT="$PUBLISHER" \
  ZLINK_CPP_E2E_PUBLISHER_HTTP_ENDPOINT="$PUBLISHER_HTTP" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$PUBLISHER_SERVER" >"$LOG_DIR/$suffix.stdout.log" 2>"$LOG_DIR/$suffix.stderr.log" &
  LAST_PID="$!"
  PUBLISHER_PID="$LAST_PID"
  PIDS+=("$LAST_PID")
  wait_port "$suffix-http" "$PUBLISHER_HTTP"
  check_operational_endpoints "$suffix" "$PUBLISHER_HTTP"
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
  ZLINK_CPP_E2E_REDIS_ENDPOINT="$REDIS_ENDPOINT" \
  ZLINK_CPP_E2E_REDIS_KEY_PREFIX="$REDIS_KEY_PREFIX" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$SUBSCRIBER" >"$LOG_DIR/$id.stdout.log" 2>"$LOG_DIR/$id.stderr.log" &
  LAST_PID="$!"
  PIDS+=("$LAST_PID")
  wait_port "$id-http" "$http"
}

stop_pid() {
  local pid="$1"
  local status
  if kill -0 "$pid" >/dev/null 2>&1; then
    kill "$pid" >/dev/null 2>&1 || true
    set +e
    wait "$pid" >/dev/null 2>&1
    status=$?
    set -e
    if [[ "$status" != "0" && "$status" != "127" && "$status" != "130" && "$status" != "143" ]]; then
      echo "stopped process $pid exited unexpectedly with status $status" >&2
      return 1
    fi
  fi
}

remember_pid_file() {
  local file="$1"
  local array_name="$2"
  if [[ ! -s "$file" ]]; then
    return 0
  fi
  local pid
  pid="$(tail -1 "$file")"
  if [[ "$pid" =~ ^[0-9]+$ ]]; then
    eval "$array_name+=(\"$pid\")"
  fi
}

stop_all_subscribers() {
  local status
  for pid in "${SUB_PIDS[@]:-}"; do
    if kill -0 "$pid" >/dev/null 2>&1; then
      kill "$pid" >/dev/null 2>&1 || true
      set +e
      wait "$pid" >/dev/null 2>&1
      status=$?
      set -e
      if [[ "$status" != "0" && "$status" != "127" && "$status" != "130" && "$status" != "143" ]]; then
        echo "stopped subscriber process $pid exited unexpectedly with status $status" >&2
        return 1
      fi
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
  ZLINK_CPP_E2E_REDIS_ENDPOINT="$REDIS_ENDPOINT" \
  ZLINK_CPP_E2E_REDIS_KEY_PREFIX="$REDIS_KEY_PREFIX" \
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
  PIDS+=("$LAST_PID")
}

verify() {
  local mode="$1"
  shift
  python3 - "$mode" "$BOUNDED_EVIDENCE_WAIT_HTTP_TIMEOUT_SECONDS" "$@" <<'PY' | tee -a "$LOG_DIR/verify.log"
import json
import sys
import urllib.request

mode = sys.argv[1]
http_timeout_seconds = float(sys.argv[2])
endpoints = sys.argv[3:]

def post_json(endpoint, path, payload):
    request = urllib.request.Request(
        endpoint + path,
        data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json"},
        method="POST")
    with urllib.request.urlopen(request, timeout=http_timeout_seconds) as response:
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
print(f"verify {mode} passed")
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
  sleep "$SCENARIO_SETTLE_SECONDS"
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
  sleep "$SCENARIO_SETTLE_SECONDS"
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
  sleep "$SCENARIO_SETTLE_SECONDS"
  touch "$START_CONTINUE"
  wait_marker "$READY"
  start_subscriber sub-3 fanout "$HTTP_3"; SUB_PIDS+=("$LAST_PID")
  sleep "$SCENARIO_SETTLE_SECONDS"
  touch "$CONTINUE"
  wait "$LATE_CLIENT_PID"
  cat "$LOG_DIR/client-late.stdout.log"
  verify late "$HTTP_1" "$HTTP_2" "$HTTP_3"
  stop_all_subscribers
fi

if should_run PS-A4 ps-a4; then
  START_READY="$LOG_DIR/ps-a4-start-ready"
  START_CONTINUE="$LOG_DIR/ps-a4-start-continue"
  RECONNECT_PID_FILE="$LOG_DIR/ps-a4-reconnect-subscriber.pid"
  start_client_waiting reconnect reconnect "$START_READY" "$START_CONTINUE" \
    ZLINK_CPP_E2E_SUBSCRIBER_EXE="$SUBSCRIBER" \
    ZLINK_CPP_E2E_RECONNECT_SUBSCRIBER_URL="$HTTP_3" \
    ZLINK_CPP_E2E_RECONNECT_SUBSCRIBER_PID_FILE="$RECONNECT_PID_FILE"
  RECONNECT_CLIENT_PID="$LAST_PID"
  wait_marker "$START_READY"
  start_subscriber sub-1 fanout "$HTTP_1"; SUB_PIDS+=("$LAST_PID")
  start_subscriber sub-2 fanout "$HTTP_2"; SUB_PIDS+=("$LAST_PID")
  sleep "$SCENARIO_SETTLE_SECONDS"
  touch "$START_CONTINUE"
  wait "$RECONNECT_CLIENT_PID"
  remember_pid_file "$RECONNECT_PID_FILE" SUB_PIDS
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
  sleep "$SCENARIO_SETTLE_SECONDS"
  touch "$START_CONTINUE"
  wait "$SLOW_CLIENT_PID"
  cat "$LOG_DIR/client-slow.stdout.log"
  verify slow "$HTTP_1" "$HTTP_2" "$HTTP_3"
  stop_all_subscribers
fi

if should_run PS-B2 ps-b2; then
  START_READY="$LOG_DIR/ps-b2-start-ready"
  START_CONTINUE="$LOG_DIR/ps-b2-start-continue"
  RESTARTED_PUBLISHER_PID_FILE="$LOG_DIR/ps-b2-restarted-publisher.pid"
  start_subscriber sub-1 fanout "$HTTP_1"; SUB_PIDS+=("$LAST_PID")
  start_subscriber sub-2 fanout "$HTTP_2"; SUB_PIDS+=("$LAST_PID")
  start_subscriber sub-3 fanout "$HTTP_3"; SUB_PIDS+=("$LAST_PID")
  start_client_waiting publisher-restart publisher-before "$START_READY" "$START_CONTINUE" \
    ZLINK_CPP_E2E_PUBLISHER_EXE="$PUBLISHER_SERVER" \
    ZLINK_CPP_E2E_RESTARTED_PUBLISHER_PID_FILE="$RESTARTED_PUBLISHER_PID_FILE"
  PUB_RESTART_CLIENT_PID="$LAST_PID"
  wait_marker "$START_READY"
  sleep "$SCENARIO_SETTLE_SECONDS"
  touch "$START_CONTINUE"
  wait "$PUB_RESTART_CLIENT_PID"
  remember_pid_file "$RESTARTED_PUBLISHER_PID_FILE" PIDS
  cat "$LOG_DIR/client-publisher-before.stdout.log"
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
  sleep "$SCENARIO_SETTLE_SECONDS"
  touch "$START_CONTINUE"
  wait "$NEGATIVE_CLIENT_PID"
  cat "$LOG_DIR/client-negative.stdout.log"
  verify negative "$HTTP_1" "$HTTP_2" "$HTTP_3"
  stop_all_subscribers
fi

snapshot_operational_evidence publisher "$PUBLISHER_HTTP" "$LOG_DIR/publisher-evidence-final.json"
echo "pubsub e2e result=passed"
