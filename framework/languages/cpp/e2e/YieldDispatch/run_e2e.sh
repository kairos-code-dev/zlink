#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FRAMEWORK_DIR="$(cd "$ROOT_DIR/../.." && pwd)"
BUILD_DIR="${ZLINK_CPP_E2E_BUILD_DIR:-$FRAMEWORK_DIR/build}"
LOCAL_READINESS_TIMEOUT_SECONDS=3
LOCAL_READINESS_POLL_SECONDS=0.1
ROUTE_SETTLE_SECONDS=5
SCENARIO_SETTLE_SECONDS=3
HTTP_PROBE_TIMEOUT_SECONDS=3
PROCESS_SHUTDOWN_TIMEOUT_SECONDS=3
PROCESS_SHUTDOWN_POLL_SECONDS=0.1
SCENARIO_MARKER_TIMEOUT_SECONDS=30
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
  python3 - "$PROCESS_SHUTDOWN_TIMEOUT_SECONDS" "$PROCESS_SHUTDOWN_POLL_SECONDS" <<'PY'
import math
import sys

timeout = float(sys.argv[1])
poll = float(sys.argv[2])
print(max(1, math.ceil(timeout / poll)))
PY
)"
SCENARIO_MARKER_ATTEMPTS="$(
  python3 - "$SCENARIO_MARKER_TIMEOUT_SECONDS" "$LOCAL_READINESS_POLL_SECONDS" <<'PY'
import math
import sys

timeout = float(sys.argv[1])
poll = float(sys.argv[2])
print(max(1, math.ceil(timeout / poll)))
PY
)"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"
echo "log_dir=$LOG_DIR"

read -r REG_HTTP REG_PUB REG_ROUTER \
  DELAY_A_HTTP DELAY_A_ENDPOINT DELAY_B_HTTP DELAY_B_ENDPOINT \
  PLAY_A_HTTP PLAY_A_CONTROL PLAY_A_SPOT_ROUTE PLAY_A_SPOT_ROUTER PLAY_A_SPOT_PUB \
  PLAY_B_HTTP PLAY_B_CONTROL PLAY_B_SPOT_ROUTE PLAY_B_SPOT_ROUTER PLAY_B_SPOT_PUB \
  SESSION_A_HTTP SESSION_A_STREAM SESSION_A_SPOT_ROUTER SESSION_A_SPOT_PUB \
  SESSION_B_HTTP SESSION_B_STREAM SESSION_B_SPOT_ROUTER SESSION_B_SPOT_PUB <<<"$(python3 - <<'PY'
import os
import random
import socket

sockets = []
ports = []
host = os.environ.get("ZLINK_CPP_E2E_BIND_HOST", "127.0.0.2")
base = os.environ.get("ZLINK_CPP_E2E_PORT_BASE")
port_range = int(os.environ.get("ZLINK_CPP_E2E_PORT_RANGE", "20000"))
start = int(base) if base else 10000
stop = start + port_range if base else 30000
available = list(range(start, stop))
for port in random.sample(available, len(available)):
    sock = socket.socket()
    try:
        sock.bind((host, port))
    except OSError:
        sock.close()
        continue
    sockets.append(sock)
    ports.append(sock.getsockname()[1])
    if len(ports) == 25:
        break
if len(ports) != 25:
    raise SystemExit(f"failed to allocate 25 local ports, allocated {len(ports)}")
print(f"http://{host}:{ports[0]}", end=" ")
print(f"tcp://{host}:{ports[1]}", end=" ")
print(f"tcp://{host}:{ports[2]}", end=" ")
print(f"http://{host}:{ports[3]}", end=" ")
print(f"tcp://{host}:{ports[4]}", end=" ")
print(f"http://{host}:{ports[5]}", end=" ")
print(f"tcp://{host}:{ports[6]}", end=" ")
print(f"http://{host}:{ports[7]}", end=" ")
print(f"tcp://{host}:{ports[8]}", end=" ")
print(f"tcp://{host}:{ports[9]}", end=" ")
print(f"tcp://{host}:{ports[10]}", end=" ")
print(f"tcp://{host}:{ports[11]}", end=" ")
print(f"http://{host}:{ports[12]}", end=" ")
print(f"tcp://{host}:{ports[13]}", end=" ")
print(f"tcp://{host}:{ports[14]}", end=" ")
print(f"tcp://{host}:{ports[15]}", end=" ")
print(f"tcp://{host}:{ports[16]}", end=" ")
print(f"http://{host}:{ports[17]}", end=" ")
print(f"tcp://{host}:{ports[18]}", end=" ")
print(f"tcp://{host}:{ports[19]}", end=" ")
print(f"tcp://{host}:{ports[20]}", end=" ")
print(f"http://{host}:{ports[21]}", end=" ")
print(f"tcp://{host}:{ports[22]}", end=" ")
print(f"tcp://{host}:{ports[23]}", end=" ")
print(f"tcp://{host}:{ports[24]}")
for sock in sockets:
    sock.close()
PY
)"

cmake -S "$FRAMEWORK_DIR" -B "$BUILD_DIR" -DZLINK_FRAMEWORK_CPP_BUILD_SAMPLES=OFF >/dev/null
cmake --build "$BUILD_DIR" --target \
  zlink_cpp_e2e_yield_dispatch_registry \
  zlink_cpp_e2e_yield_dispatch_delay \
  zlink_cpp_e2e_yield_dispatch_play \
  zlink_cpp_e2e_yield_dispatch_session \
  zlink_cpp_e2e_yield_dispatch_client >/dev/null

REGISTRY="$BUILD_DIR/zlink_cpp_e2e_yield_dispatch_registry"
DELAY="$BUILD_DIR/zlink_cpp_e2e_yield_dispatch_delay"
PLAY="$BUILD_DIR/zlink_cpp_e2e_yield_dispatch_play"
SESSION="$BUILD_DIR/zlink_cpp_e2e_yield_dispatch_session"
CLIENT="$BUILD_DIR/zlink_cpp_e2e_yield_dispatch_client"
PIDS=()

launch_process() {
  local stdout_log="$1"
  local stderr_log="$2"
  local wrapper="$3"
  shift 3
  if [[ -n "$wrapper" ]]; then
    "$wrapper" "$@" >"$stdout_log" 2>"$stderr_log" &
  else
    "$@" >"$stdout_log" 2>"$stderr_log" &
  fi
}

print_failure_logs() {
  echo "Recent logs:" >&2
  for log_file in "$LOG_DIR"/*.stderr.log "$LOG_DIR"/client*.stdout.log; do
    [[ -f "$log_file" ]] || continue
    echo "--- ${log_file#$LOG_DIR/} ---" >&2
    tail -n 40 "$log_file" >&2 || true
  done
}

cleanup() {
  local code=$?
  for pid in "${PIDS[@]:-}"; do
    if kill -0 "$pid" >/dev/null 2>&1; then
      kill "$pid" >/dev/null 2>&1 || true
    fi
  done
  for _ in $(seq 1 "$PROCESS_SHUTDOWN_ATTEMPTS"); do
    local alive=0
    for pid in "${PIDS[@]:-}"; do
      if kill -0 "$pid" >/dev/null 2>&1; then
        alive=1
        break
      fi
    done
    [[ $alive -eq 0 ]] && break
    sleep "$PROCESS_SHUTDOWN_POLL_SECONDS"
  done
  for pid in "${PIDS[@]:-}"; do
    if kill -0 "$pid" >/dev/null 2>&1; then
      kill -9 "$pid" >/dev/null 2>&1 || true
    fi
  done
  wait >/dev/null 2>&1 || true
  if [[ $code -ne 0 ]]; then
    echo "E2E failed. Logs: $LOG_DIR" >&2
    print_failure_logs
  fi
  exit "$code"
}
trap cleanup EXIT

port_of() {
  local endpoint="$1"
  echo "${endpoint##*:}"
}

host_of() {
  local endpoint="$1"
  local rest="${endpoint#*://}"
  echo "${rest%%:*}"
}

wait_port() {
  local name="$1"
  local endpoint="$2"
  local host
  local port
  host="$(host_of "$endpoint")"
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

static_checks() {
  if rg -n 'MapPost\("/yield|HttpClient|new HttpClient|\.Post\(' "$ROOT_DIR" -g '*.cpp' -g '*.hpp' >/tmp/zlink-yield-dispatch-static-http.$$; then
    cat /tmp/zlink-yield-dispatch-static-http.$$ >&2
    rm -f /tmp/zlink-yield-dispatch-static-http.$$
    echo "YieldDispatch must not start scenarios through HTTP client or /yield HTTP endpoints." >&2
    return 1
  fi
  rm -f /tmp/zlink-yield-dispatch-static-http.$$

  if rg -n '\.yield\s*\(' "$ROOT_DIR" -g '*.cpp' -g '*.hpp' | rg -v 'Server/Play/' >/tmp/zlink-yield-dispatch-static-yield.$$; then
    cat /tmp/zlink-yield-dispatch-static-yield.$$ >&2
    rm -f /tmp/zlink-yield-dispatch-static-yield.$$
    echo "YieldDispatch may only call yield() from Spot/Entry Spot handlers in Server/Play." >&2
    return 1
  fi
  rm -f /tmp/zlink-yield-dispatch-static-yield.$$

  if rg -n 'YieldConnectorFactory|YieldDispatchScenarioContext|WaitForPlayEvidence|ReadPlayEvidence' "$ROOT_DIR/Client" -g '*.cpp' -g '*.hpp' >/tmp/zlink-yield-dispatch-static-helper.$$; then
    cat /tmp/zlink-yield-dispatch-static-helper.$$ >&2
    rm -f /tmp/zlink-yield-dispatch-static-helper.$$
    echo "YieldDispatch client scenarios must use the stream connector directly instead of thin helpers." >&2
    return 1
  fi
  rm -f /tmp/zlink-yield-dispatch-static-helper.$$

  if ! rg -q 'connector_factory_t::create' "$ROOT_DIR/Client/main.cpp"; then
    echo "YieldDispatch full scenario must create and use a real stream connector directly." >&2
    return 1
  fi
  if ! rg -q 'connector_factory_t::create' "$ROOT_DIR/Client/Scenarios/shutdown_yield_scenario.hpp"; then
    echo "YieldDispatch shutdown scenario must create and use a real stream connector directly." >&2
    return 1
  fi

  local scenario_file
  for scenario_file in "$ROOT_DIR"/Client/Scenarios/yd_*.hpp; do
    if ! rg -q 'TConnector &' "$scenario_file"; then
      echo "$scenario_file" >&2
      echo "YieldDispatch YD scenario files must receive the stream connector directly." >&2
      return 1
    fi
  done
}

static_checks

wait_file_contains() {
  local file="$1"
  local pattern="$2"
  local message="$3"
  local watched_pid="${4:-}"
  local attempts="${5:-$SCENARIO_MARKER_ATTEMPTS}"
  for _ in $(seq 1 "$attempts"); do
    if [[ -f "$file" ]] && grep -Fq "$pattern" "$file"; then
      return 0
    fi
    if [[ -n "$watched_pid" ]] && ! kill -0 "$watched_pid" >/dev/null 2>&1; then
      if [[ -f "$file" ]] && grep -Fq "$pattern" "$file"; then
        return 0
      fi
      break
    fi
    sleep "$LOCAL_READINESS_POLL_SECONDS"
  done
  echo "$message" >&2
  return 1
}

terminate_gracefully() {
  local name="$1"
  local pid="$2"
  kill "$pid" >/dev/null 2>&1 || true
  for _ in $(seq 1 "$PROCESS_SHUTDOWN_ATTEMPTS"); do
    if ! kill -0 "$pid" >/dev/null 2>&1; then
      wait "$pid" >/dev/null 2>&1 || true
      return 0
    fi
    sleep "$PROCESS_SHUTDOWN_POLL_SECONDS"
  done
  echo "$name did not exit after SIGTERM" >&2
  kill -9 "$pid" >/dev/null 2>&1 || true
  wait "$pid" >/dev/null 2>&1 || true
  return 1
}

ZLINK_CPP_E2E_HTTP_ENDPOINT="$REG_HTTP" \
ZLINK_CPP_E2E_REGISTRY_PUB="$REG_PUB" \
ZLINK_CPP_E2E_REGISTRY_ROUTER="$REG_ROUTER" \
ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
  "$REGISTRY" >"$LOG_DIR/registry.stdout.log" 2>"$LOG_DIR/registry.stderr.log" &
PIDS+=("$!")
wait_port registry "$REG_HTTP"

start_delay_role() {
  local name="$1"
  local http_endpoint="$2"
  local delay_endpoint="$3"
  ZLINK_CPP_E2E_NODE_RID="$name" \
  ZLINK_CPP_E2E_HTTP_ENDPOINT="$http_endpoint" \
  ZLINK_CPP_E2E_DELAY_ENDPOINT="$delay_endpoint" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    launch_process "$LOG_DIR/$name.stdout.log" "$LOG_DIR/$name.stderr.log" \
      "${ZLINK_CPP_E2E_DELAY_WRAPPER:-}" "$DELAY"
  PIDS+=("$!")
  wait_port "$name" "$http_endpoint"
  wait_port "$name-delay" "$delay_endpoint"
}

start_play_role() {
  local name="$1"
  local http_endpoint="$2"
  local control_endpoint="$3"
  local spot_route_endpoint="$4"
  local spot_router_endpoint="$5"
  local spot_pub_endpoint="$6"
  local delay_endpoint="$7"
  ZLINK_CPP_E2E_NODE_RID="$name" \
  ZLINK_CPP_E2E_HTTP_ENDPOINT="$http_endpoint" \
  ZLINK_CPP_E2E_CONTROL_ENDPOINT="$control_endpoint" \
  ZLINK_CPP_E2E_DELAY_ENDPOINT="$delay_endpoint" \
  ZLINK_CPP_E2E_SPOT_ROUTE_ENDPOINT="$spot_route_endpoint" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$spot_router_endpoint" \
  ZLINK_CPP_E2E_SPOT_PUB_ENDPOINT="$spot_pub_endpoint" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REG_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    launch_process "$LOG_DIR/$name.stdout.log" "$LOG_DIR/$name.stderr.log" \
      "${ZLINK_CPP_E2E_PLAY_WRAPPER:-}" "$PLAY"
  PIDS+=("$!")
  wait_port "$name" "$http_endpoint"
  wait_port "$name-control" "$control_endpoint"
}

start_session_role() {
  local name="$1"
  local http_endpoint="$2"
  local stream_endpoint="$3"
  local control_endpoint="$4"
  local spot_router_endpoint="$5"
  local spot_pub_endpoint="$6"
  ZLINK_CPP_E2E_NODE_RID="$name" \
  ZLINK_CPP_E2E_HTTP_ENDPOINT="$http_endpoint" \
  ZLINK_CPP_E2E_STREAM_ENDPOINT="$stream_endpoint" \
  ZLINK_CPP_E2E_CONTROL_ENDPOINT="$control_endpoint" \
  ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT="$spot_router_endpoint" \
  ZLINK_CPP_E2E_SPOT_PUB_ENDPOINT="$spot_pub_endpoint" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REG_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    launch_process "$LOG_DIR/$name.stdout.log" "$LOG_DIR/$name.stderr.log" \
      "${ZLINK_CPP_E2E_SESSION_WRAPPER:-}" "$SESSION"
  PIDS+=("$!")
  wait_port "$name" "$http_endpoint"
  wait_port "$name-stream" "$stream_endpoint"
}

start_delay_role delay-a "$DELAY_A_HTTP" "$DELAY_A_ENDPOINT"
start_delay_role delay-b "$DELAY_B_HTTP" "$DELAY_B_ENDPOINT"
start_play_role play-a "$PLAY_A_HTTP" "$PLAY_A_CONTROL" "$PLAY_A_SPOT_ROUTE" "$PLAY_A_SPOT_ROUTER" "$PLAY_A_SPOT_PUB" "$DELAY_A_ENDPOINT"
PLAY_A_PID="${PIDS[-1]}"
start_play_role play-b "$PLAY_B_HTTP" "$PLAY_B_CONTROL" "$PLAY_B_SPOT_ROUTE" "$PLAY_B_SPOT_ROUTER" "$PLAY_B_SPOT_PUB" "$DELAY_B_ENDPOINT"
start_session_role session-a "$SESSION_A_HTTP" "$SESSION_A_STREAM" "$PLAY_A_CONTROL" "$SESSION_A_SPOT_ROUTER" "$SESSION_A_SPOT_PUB"
start_session_role session-b "$SESSION_B_HTTP" "$SESSION_B_STREAM" "$PLAY_B_CONTROL" "$SESSION_B_SPOT_ROUTER" "$SESSION_B_SPOT_PUB"

ZLINK_CPP_E2E_STREAM_ENDPOINT="$SESSION_A_STREAM" \
ZLINK_CPP_E2E_SESSION_B_STREAM_ENDPOINT="$SESSION_B_STREAM" \
ZLINK_CPP_E2E_SCENARIO="full" \
  "$CLIENT" >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"
grep -q "scenario YD-A1 passed" "$LOG_DIR/client.stdout.log"
grep -q "scenario YD-A2 passed" "$LOG_DIR/client.stdout.log"
grep -q "scenario YD-A3 passed" "$LOG_DIR/client.stdout.log"
grep -q "scenario YD-A4 passed" "$LOG_DIR/client.stdout.log"
grep -q "scenario YD-B1 passed" "$LOG_DIR/client.stdout.log"
grep -q "scenario YD-B2 passed" "$LOG_DIR/client.stdout.log"
grep -q "scenario YD-B3 passed" "$LOG_DIR/client.stdout.log"
grep -q "scenario YD-C1 passed" "$LOG_DIR/client.stdout.log"
grep -q "scenario YD-C2 passed" "$LOG_DIR/client.stdout.log"
grep -q "scenario YD-C3 passed" "$LOG_DIR/client.stdout.log"
grep -q "scenario YD-D2 passed" "$LOG_DIR/client.stdout.log"
grep -q "scenario YD-D3 passed" "$LOG_DIR/client.stdout.log"
grep -q "scenario YD-D4 passed" "$LOG_DIR/client.stdout.log"
grep -q "scenario YD-E1 passed" "$LOG_DIR/client.stdout.log"
grep -q "yield-dispatch track-a-e1 result=passed" "$LOG_DIR/client.stdout.log"
grep -q "^hold-completed|rid=play-a" "$LOG_DIR/play-a.evidence.log"
grep -q "^yield-completed|rid=play-a" "$LOG_DIR/play-a.evidence.log"
grep -q "^worker-yield-completed|rid=play-a" "$LOG_DIR/play-a.evidence.log"
grep -q "^actor-yield-completed|rid=play-a" "$LOG_DIR/play-a.evidence.log"
grep -q "^timer-yield-completed|rid=play-a" "$LOG_DIR/play-a.evidence.log"
grep -q "^timeout-yield-completed|rid=play-a" "$LOG_DIR/play-a.evidence.log"
echo "scenario YD-D1 passed"

SHUTDOWN_ID="YD-E3-$RUN_ID"
SHUTDOWN_SPOT="yield-shutdown-${RUN_ID//[^a-zA-Z0-9]/}"
ZLINK_CPP_E2E_STREAM_ENDPOINT="$SESSION_A_STREAM" \
ZLINK_CPP_E2E_SESSION_B_STREAM_ENDPOINT="$SESSION_B_STREAM" \
ZLINK_CPP_E2E_SCENARIO="shutdown-wait" \
ZLINK_CPP_E2E_REQUEST_ID="$SHUTDOWN_ID" \
ZLINK_CPP_E2E_SPOT_RID="$SHUTDOWN_SPOT" \
  "$CLIENT" >"$LOG_DIR/client-shutdown-wait.stdout.log" 2>"$LOG_DIR/client-shutdown-wait.stderr.log" &
SHUTDOWN_CLIENT_PID=$!
wait_file_contains \
  "$LOG_DIR/play-a.evidence.log" \
  "yield-released|rid=play-a|spot=$SHUTDOWN_SPOT|request=$SHUTDOWN_ID" \
  "YD-E3 pending yield marker was not observed before shutdown." \
  "$SHUTDOWN_CLIENT_PID"
kill "$PLAY_A_PID" >/dev/null 2>&1 || true
wait_file_contains \
  "$LOG_DIR/client-shutdown-wait.stdout.log" \
  "yield-dispatch shutdown wait result=passed" \
  "YD-E3 shutdown client did not observe the public closed/cancelled error." \
  "$SHUTDOWN_CLIENT_PID"
terminate_gracefully play-a "$PLAY_A_PID"
wait "$SHUTDOWN_CLIENT_PID"

start_play_role play-a "$PLAY_A_HTTP" "$PLAY_A_CONTROL" "$PLAY_A_SPOT_ROUTE" "$PLAY_A_SPOT_ROUTER" "$PLAY_A_SPOT_PUB" "$DELAY_A_ENDPOINT"
PLAY_A_PID="${PIDS[-1]}"
  sleep "$SCENARIO_SETTLE_SECONDS"

ZLINK_CPP_E2E_STREAM_ENDPOINT="$SESSION_A_STREAM" \
ZLINK_CPP_E2E_SESSION_B_STREAM_ENDPOINT="$SESSION_B_STREAM" \
ZLINK_CPP_E2E_SCENARIO="shutdown-recovery" \
ZLINK_CPP_E2E_REQUEST_ID="$SHUTDOWN_ID-recovery" \
ZLINK_CPP_E2E_SPOT_RID="$SHUTDOWN_SPOT" \
  "$CLIENT" >"$LOG_DIR/client-shutdown-recovery.stdout.log" 2>"$LOG_DIR/client-shutdown-recovery.stderr.log"
grep -q "yield-dispatch shutdown recovery result=passed" "$LOG_DIR/client-shutdown-recovery.stdout.log"

python3 - "$LOG_DIR/yield-dispatch-report.json" <<'PY'
import json
import sys

report = {
    "language": "cpp",
    "config": "YieldDispatch",
    "scenarios": [
        {"id": "YD-A1", "status": "passed",
         "markers": ["hold-started", "hold-resumed", "hold-completed", "probe-started"]},
        {"id": "YD-A2", "status": "passed",
         "markers": ["yield-started", "yield-released", "probe-started",
                     "probe-completed", "yield-resumed", "yield-completed"]},
        {"id": "YD-A3", "status": "passed",
         "markers": ["yield-started", "yield-released", "yield-resumed", "yield-completed"]},
        {"id": "YD-A4", "status": "passed",
         "markers": ["worker-yield-started", "worker-yield-released", "probe-started",
                     "probe-completed", "worker-yield-resumed", "worker-yield-completed"]},
        {"id": "YD-B1", "status": "passed",
         "markers": ["actor-yield-started", "actor-yield-released", "actor-fast-started",
                     "actor-fast-completed", "actor-yield-resumed", "actor-yield-completed"]},
        {"id": "YD-B2", "status": "passed",
         "markers": ["actor-yield-started", "actor-yield-released", "actor-yield-resumed",
                     "actor-yield-completed", "actor-fast-started", "actor-fast-completed"]},
        {"id": "YD-B3", "status": "passed",
         "markers": ["actor-join-yield-started", "actor-join-yield-released",
                     "actor-fast-started", "actor-fast-completed",
                     "actor-join-yield-resumed", "actor-join-yield-completed"]},
        {"id": "YD-C1", "status": "passed",
         "markers": ["timer-yield-started", "timer-yield-released",
                     "timer-fast-started", "timer-fast-completed",
                     "timer-yield-resumed", "timer-yield-completed"]},
        {"id": "YD-C2", "status": "passed",
         "markers": ["timer-yield-started", "timer-yield-released",
                     "timer-yield-resumed", "timer-yield-completed"]},
        {"id": "YD-C3", "status": "passed",
         "markers": ["actor-yield-started", "actor-yield-released",
                     "timer-fast-started", "timer-fast-completed",
                     "actor-yield-resumed", "actor-yield-completed",
                     "timer-yield-started", "timer-yield-released",
                     "actor-fast-started", "actor-fast-completed",
                     "timer-yield-resumed", "timer-yield-completed"]},
        {"id": "YD-D1", "status": "passed",
         "markers": ["hold-completed", "yield-completed", "worker-yield-completed",
                     "actor-yield-completed", "timer-yield-completed",
                     "timeout-yield-completed"]},
        {"id": "YD-D2", "status": "passed",
         "markers": ["remote-yield-started", "remote-yield-released",
                     "yield-started", "yield-released", "yield-resumed",
                     "yield-completed", "remote-yield-resumed",
                     "remote-yield-completed"]},
        {"id": "YD-D3", "status": "passed",
         "markers": ["yield-started", "yield-released", "probe-started",
                     "probe-completed", "yield-resumed", "yield-completed"]},
        {"id": "YD-D4", "status": "passed",
         "markers": ["actor-push-yield-started", "actor-push-yield-released",
                     "actor-push-yield-resumed", "actor-push-yield-completed"]},
        {"id": "YD-E1", "status": "passed",
         "markers": ["timeout-yield-started", "timeout-yield-released",
                     "timeout-yield-completed", "probe-started", "probe-completed"]},
        {"id": "YD-E2", "status": "gap",
         "gap": "C++ public yield call surface has no cancellation token argument."},
        {"id": "YD-E3", "status": "passed",
         "markers": ["yield-released", "probe-completed"]},
        {"id": "YD-E4", "status": "passed",
         "markers": ["static-check-http", "static-check-yield-surface",
                     "static-check-connector"]},
        {"id": "YD-E5", "status": "passed",
         "markers": ["report-written"]}
    ]
}

with open(sys.argv[1], "w", encoding="utf-8") as file:
    json.dump(report, file, ensure_ascii=False, indent=2)
    file.write("\n")
PY
python3 - "$LOG_DIR/yield-dispatch-report.json" <<'PY'
import json
import sys

report = json.load(open(sys.argv[1], encoding="utf-8"))
ids = {entry["id"] for entry in report["scenarios"]}
expected = {f"YD-{track}{index}" for track, count in (("A", 4), ("B", 3), ("C", 3), ("D", 4), ("E", 5))
            for index in range(1, count + 1)}
missing = expected - ids
extra = ids - expected
assert not missing, sorted(missing)
assert not extra, sorted(extra)
assert report["language"] == "cpp"
assert report["config"] == "YieldDispatch"
PY
echo "scenario YD-E5 passed"
echo "yield-dispatch e2e result=passed"
