#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${ZLINK_CPP_E2E_BUILD_DIR:-$CPP_DIR/build}"

read -r REGISTRY_PUB REGISTRY_ROUTER API_A API_B ROUTE_A ROUTE_B DEALER_A DEALER_B API_B_GREEN ROUTE_B_GREEN HTTP_REGISTRY HTTP_A HTTP_B HTTP_CONSUMER HTTP_B_GREEN CLIENT_ROUTE <<<"$(python3 - <<'PY'
import socket

sockets = []
ports = []
for _ in range(16):
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    sockets.append(s)
    ports.append(s.getsockname()[1])
print(" ".join(f"tcp://127.0.0.1:{p}" for p in ports[:10]), end=" ")
print(" ".join(f"http://127.0.0.1:{p}" for p in ports[10:15]), end=" ")
print(f"tcp://127.0.0.1:{ports[15]}")
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
  zlink_cpp_e2e_resilience_lifecycle_consumer \
  zlink_cpp_e2e_resilience_lifecycle_client >/dev/null

REGISTRY="$BUILD_DIR/zlink_cpp_e2e_resilience_lifecycle_registry"
PROVIDER="$BUILD_DIR/zlink_cpp_e2e_resilience_lifecycle_provider"
CONSUMER="$BUILD_DIR/zlink_cpp_e2e_resilience_lifecycle_consumer"
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
  for _ in $(seq 1 60); do
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

crash_provider() {
  local http="$1"
  local pid="$2"
  python3 - "$http" <<'PY'
import sys
import urllib.request

base = sys.argv[1]
request = urllib.request.Request(f"{base}/admin/crash", data=b"", method="POST")
with urllib.request.urlopen(request, timeout=3):
    pass
PY
  for _ in $(seq 1 12); do
    if ! python3 - "$http" <<'PY' >/dev/null 2>&1
import sys
import urllib.request

with urllib.request.urlopen(f"{sys.argv[1]}/health", timeout=1) as response:
    if response.status != 200:
        raise SystemExit(1)
PY
    then
      wait "$pid" >/dev/null 2>&1 || true
      return 0
    fi
    sleep 0.25
  done
  echo "Timed out waiting for provider crash at $http" >&2
  return 1
}

set_server_weight() {
  local http="$1"
  local weight="$2"
  local path="/admin/server-weight?weight=$weight"
  if [[ "$weight" == "0" ]]; then
    path="/admin/drain"
  elif [[ "$weight" == "100" ]]; then
    path="/admin/restore"
  fi
  python3 - "$http" "$weight" "$path" <<'PY'
import json
import sys
import urllib.request

base = sys.argv[1]
weight = sys.argv[2]
path = sys.argv[3]
request = urllib.request.Request(
    f"{base}{path}",
    data=b"",
    method="POST",
)
with urllib.request.urlopen(request, timeout=3) as response:
    if response.status != 200:
        raise SystemExit(f"unexpected status {response.status}")
wait = urllib.request.Request(
    f"{base}/admin/weight/wait",
    data=json.dumps({"expected": int(weight)}).encode("utf-8"),
    headers={"Content-Type": "application/json"},
    method="POST",
)
with urllib.request.urlopen(wait, timeout=3) as response:
    if response.status != 200:
        raise SystemExit(f"unexpected weight wait status {response.status}")
PY
}

post_consumer_profile() {
  local marker="$1"
  post_consumer_profile_request "/profile/request" "$marker" "" ""
}

post_consumer_profile_request() {
  local path="$1"
  local value="$2"
  local marker="$3"
  local expected_provider="${4:-}"
  python3 - "$HTTP_CONSUMER" "$path" "$value" "$marker" "$expected_provider" <<'PY'
import json
import sys
import urllib.request

base = sys.argv[1]
path = sys.argv[2]
value = sys.argv[3]
marker = sys.argv[4]
expected_provider = sys.argv[5]
payload = {"value": value}
if marker:
    payload["marker"] = marker
request = urllib.request.Request(
    f"{base}{path}",
    data=json.dumps(payload).encode("utf-8"),
    headers={"Content-Type": "application/json"},
    method="POST",
)
with urllib.request.urlopen(request, timeout=3) as response:
    body = response.read().decode("utf-8")
    if response.status != 200:
        raise SystemExit(f"unexpected status {response.status}: {body}")
    payload = json.loads(body)
    if payload.get("value") != f"profile:{value}":
        raise SystemExit(f"unexpected payload {payload}")
    if marker and payload.get("marker") != marker:
        raise SystemExit(f"unexpected marker payload {payload}")
    if expected_provider and payload.get("provider_rid") != expected_provider:
        raise SystemExit(f"unexpected payload {payload}")
PY
}

post_consumer_profile_burst() {
  local value="$1"
  local marker_prefix="$2"
  local count="$3"
  python3 - "$HTTP_CONSUMER" "$value" "$marker_prefix" "$count" <<'PY'
import concurrent.futures
import json
import sys
import urllib.error
import urllib.request

base = sys.argv[1]
value = sys.argv[2]
marker_prefix = sys.argv[3]
count = int(sys.argv[4])

def post(index):
    marker = f"{marker_prefix}{index}"
    request = urllib.request.Request(
        f"{base}/profile/request",
        data=json.dumps({"value": value, "marker": marker}).encode("utf-8"),
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        response = urllib.request.urlopen(request, timeout=3)
    except urllib.error.HTTPError as error:
        body = error.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"unexpected status {error.code} for {marker}: {body}") from error
    with response:
        body = response.read().decode("utf-8")
        if response.status != 200:
            raise RuntimeError(f"unexpected status {response.status} for {marker}: {body}")
        payload = json.loads(body)
        if payload.get("value") != f"profile:{value}":
            raise RuntimeError(f"unexpected payload {payload}")

with concurrent.futures.ThreadPoolExecutor(max_workers=16) as executor:
    list(executor.map(post, range(count)))
PY
}

post_consumer_command_burst() {
  local marker_prefix="$1"
  local count="$2"
  python3 - "$HTTP_CONSUMER" "$marker_prefix" "$count" <<'PY'
import concurrent.futures
import json
import sys
import urllib.request

base = sys.argv[1]
marker_prefix = sys.argv[2]
count = int(sys.argv[3])

def post(index):
    marker = f"{marker_prefix}{index}"
    request = urllib.request.Request(
        f"{base}/profile/command",
        data=json.dumps({"command_id": marker, "marker": marker}).encode("utf-8"),
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=3) as response:
        body = response.read().decode("utf-8")
        if response.status != 200:
            raise RuntimeError(f"unexpected status {response.status}: {body}")
        payload = json.loads(body)
        if payload.get("status") != "sent":
            raise RuntimeError(f"unexpected command payload {payload}")

with concurrent.futures.ThreadPoolExecutor(max_workers=16) as executor:
    list(executor.map(post, range(count)))
PY
}

wait_provider_evidence_value_prefix() {
  local prefix="$1"
  local expected_provider="${2:-}"
  python3 - "$HTTP_A" "$HTTP_B" "$prefix" "$expected_provider" <<'PY'
import json
import sys
import time
import urllib.request

urls = [sys.argv[1], sys.argv[2]]
prefix = sys.argv[3]
expected_provider = sys.argv[4]
deadline = time.monotonic() + 15
while time.monotonic() < deadline:
    for base in urls:
        try:
            with urllib.request.urlopen(f"{base}/evidence", timeout=2) as response:
                payload = json.loads(response.read().decode("utf-8"))
        except Exception:
            continue
        for entry in payload.get("entries", []):
            if not str(entry.get("value", "")).startswith(prefix):
                continue
            if expected_provider and entry.get("provider_rid") != expected_provider:
                continue
            raise SystemExit(0)
    time.sleep(0.1)
detail = f" provider {expected_provider}" if expected_provider else ""
raise SystemExit(f"timed out waiting for provider evidence value prefix {prefix}{detail}")
PY
}

wait_registry_topology() {
  local routing_id="$1"
  local state="${2:-Ready}"
  local expected_count="${3:-1}"
  python3 - "$HTTP_REGISTRY" "$routing_id" "$state" "$expected_count" <<'PY'
import json
import sys
import urllib.request

base = sys.argv[1]
routing_id = sys.argv[2]
state = sys.argv[3]
expected_count = int(sys.argv[4])
request = urllib.request.Request(
    f"{base}/topology/wait",
    data=json.dumps({
        "routing_id": routing_id,
        "state": state,
        "expected_count": expected_count,
        "timeout_milliseconds": 30000,
    }).encode("utf-8"),
    headers={"Content-Type": "application/json"},
    method="POST",
)
with urllib.request.urlopen(request, timeout=35) as response:
    body = response.read().decode("utf-8")
    if response.status != 200:
        raise SystemExit(f"unexpected topology wait status {response.status}: {body}")
    entries = json.loads(body)
    count = sum(1 for entry in entries
                if entry.get("routing_id") == routing_id and entry.get("state") == state)
    if count != expected_count:
        raise SystemExit(f"unexpected topology count {count}: {entries}")
PY
}

post_consumer_new_client_profile() {
  local marker="$1"
  post_consumer_profile_request "/profile/request/new-client" "$marker" "" ""
}

post_consumer_new_client_burst() {
  local value="$1"
  local marker_prefix="$2"
  local count="$3"
  local expected_provider="${4:-}"
  for index in $(seq 0 $((count - 1))); do
    post_consumer_profile_request \
      "/profile/request/new-client" \
      "$value" \
      "${marker_prefix}${index}" \
      "$expected_provider"
  done
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
  ZLINK_CPP_E2E_EVIDENCE_FILE="$LOG_DIR/$rid.evidence.log" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$PROVIDER" >"$LOG_DIR/$rid-$instance.stdout.log" 2>"$LOG_DIR/$rid-$instance.stderr.log" &
  LAST_PID="$!"
  PIDS+=("$LAST_PID")
  wait_port "$rid-api" "$api"
  wait_port "$rid-route" "$route"
  wait_port "$rid-http" "$http"
}

start_consumer() {
  ZLINK_CPP_E2E_HTTP_ENDPOINT="$HTTP_CONSUMER" \
  ZLINK_CPP_E2E_REGISTRY_ROUTER="$REGISTRY_ROUTER" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
    "$CONSUMER" >"$LOG_DIR/consumer.stdout.log" 2>"$LOG_DIR/consumer.stderr.log" &
  LAST_PID="$!"
  PIDS+=("$LAST_PID")
  wait_port consumer-http "$HTTP_CONSUMER"
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
  ZLINK_CPP_E2E_HTTP_B_GREEN_ENDPOINT="$HTTP_B_GREEN" \
  ZLINK_CPP_E2E_API_A_EVIDENCE_FILE="$LOG_DIR/api-a.evidence.log" \
  ZLINK_CPP_E2E_API_B_EVIDENCE_FILE="$LOG_DIR/api-b.evidence.log" \
  ZLINK_CPP_E2E_HTTP_CONSUMER_ENDPOINT="$HTTP_CONSUMER" \
  ZLINK_CPP_E2E_CLIENT_ROUTE_ENDPOINT="$CLIENT_ROUTE" \
  ZLINK_CPP_E2E_LOG_DIR="$LOG_DIR" \
  "$@" \
    "$CLIENT" >"$LOG_DIR/client-$suffix.stdout.log" 2>"$LOG_DIR/client-$suffix.stderr.log"
}

post_consumer_timeout_request() {
  local marker="$1"
  python3 - "$HTTP_CONSUMER" "$marker" <<'PY'
import json
import sys
import urllib.request

base = sys.argv[1]
marker = sys.argv[2]
request = urllib.request.Request(
    f"{base}/profile/request/timeout/100",
    data=json.dumps({"value": marker}).encode("utf-8"),
    headers={"Content-Type": "application/json"},
    method="POST",
)
with urllib.request.urlopen(request, timeout=3) as response:
    body = response.read().decode("utf-8")
    if response.status != 200:
        raise SystemExit(f"unexpected status {response.status}: {body}")
    payload = json.loads(body)
    if payload.get("failed") is not True:
        raise SystemExit(f"expected failed timeout payload, got {payload}")
PY
}

post_consumer_missing_request() {
  local marker="$1"
  python3 - "$HTTP_CONSUMER" "$marker" <<'PY'
import json
import sys
import urllib.request

base = sys.argv[1]
marker = sys.argv[2]
request = urllib.request.Request(
    f"{base}/profile/request/missing",
    data=json.dumps({"value": marker}).encode("utf-8"),
    headers={"Content-Type": "application/json"},
    method="POST",
)
with urllib.request.urlopen(request, timeout=3) as response:
    body = response.read().decode("utf-8")
    if response.status != 200:
        raise SystemExit(f"unexpected status {response.status}: {body}")
    payload = json.loads(body)
    if payload.get("failed") is not True:
        raise SystemExit(f"expected failed missing-request payload, got {payload}")
PY
}

start_registry
REGISTRY_PID="$LAST_PID"

start_provider api-a "$API_A" "$ROUTE_A" "$DEALER_A" "$HTTP_A"
API_A_PID="$LAST_PID"
start_provider api-b "$API_B" "$ROUTE_B" "$DEALER_B" "$HTTP_B"
API_B_PID="$LAST_PID"
start_consumer
CONSUMER_PID="$LAST_PID"
sleep 1
post_consumer_profile "rl-consumer-smoke"
grep -q "message flow" "$LOG_DIR/consumer-flow.log"
echo "scenario RL-consumer passed"
post_consumer_new_client_profile "rl-c1-new-client"
grep -q "message flow" "$LOG_DIR/storm-rl-c1-new-client-flow.log"
echo "scenario RL-C1 consumer passed"
post_consumer_timeout_request "slow"
post_consumer_profile "rl-b1-follow-up"
echo "scenario RL-B1 passed"
post_consumer_missing_request "rl-d3-missing"
grep -Eq "reason=handler_missing.*action=reply_error.*packet=MissingProfileReq" \
  "$LOG_DIR/api-a-flow.log" "$LOG_DIR/api-b-flow.log"
echo "scenario RL-D3 passed"
run_client observer-fault rl-d2 env
grep -q "scenario RL-D2 passed" "$LOG_DIR/client-rl-d2.stdout.log"
echo "scenario RL-D2 passed"
stop_pid "$API_A_PID"
stop_pid "$API_B_PID"

start_provider api-a "$API_A" "$ROUTE_A" "$DEALER_A" "$HTTP_A" api-a-v1
API_A_PID="$LAST_PID"
READY="$LOG_DIR/rl-a1-ready"
CONTINUE="$LOG_DIR/rl-a1-continue"
run_client rl-a1 rl-a1 env \
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
grep -q "scenario RL-A1 client passed" "$LOG_DIR/client-rl-a1.stdout.log"
echo "scenario RL-A1 passed"
stop_pid "$API_A_PID"

start_provider api-a "$API_A" "$ROUTE_A" "$DEALER_A" "$HTTP_A" api-a-v1
API_A_PID="$LAST_PID"
READY="$LOG_DIR/rl-a2-ready"
CONTINUE="$LOG_DIR/rl-a2-continue"
run_client rl-a2 rl-a2 env \
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
grep -q "scenario RL-A2 client passed" "$LOG_DIR/client-rl-a2.stdout.log"
echo "scenario RL-A2 passed"
stop_pid "$API_A_PID"

start_provider api-a "$API_A" "$ROUTE_A" "$DEALER_A" "$HTTP_A"
API_A_PID="$LAST_PID"
start_provider api-b "$API_B" "$ROUTE_B" "$DEALER_B" "$HTTP_B"
API_B_PID="$LAST_PID"
READY="$LOG_DIR/rl-b3-ready"
CONTINUE="$LOG_DIR/rl-b3-continue"
run_client rl-b3 rl-b3 env \
  ZLINK_CPP_E2E_READY_FILE="$READY" \
  ZLINK_CPP_E2E_CONTINUE_FILE="$CONTINUE" &
B3_CLIENT_PID="$!"
wait_marker "$READY"
stop_pid "$API_B_PID"
sleep 5
touch "$CONTINUE"
wait "$B3_CLIENT_PID"
grep -q "scenario RL-B3 client passed" "$LOG_DIR/client-rl-b3.stdout.log"
echo "scenario RL-B3 passed"
stop_pid "$API_A_PID"

start_provider api-a "$API_A" "$ROUTE_A" "$DEALER_A" "$HTTP_A"
API_A_PID="$LAST_PID"
start_provider api-b "$API_B" "$ROUTE_B" "$DEALER_B" "$HTTP_B"
API_B_PID="$LAST_PID"
READY="$LOG_DIR/rl-b2-ready"
CONTINUE="$LOG_DIR/rl-b2-continue"
DRAINED="$LOG_DIR/rl-b2-after-crash"
RESTORE="$LOG_DIR/rl-b2-restart"
run_client inflight-crash rl-b2 env \
  ZLINK_CPP_E2E_READY_FILE="$READY" \
  ZLINK_CPP_E2E_CONTINUE_FILE="$CONTINUE" \
  ZLINK_CPP_E2E_DRAINED_FILE="$DRAINED" \
  ZLINK_CPP_E2E_RESTORE_FILE="$RESTORE" &
B2_CLIENT_PID="$!"
wait_marker "$READY"
kill_pid "$API_B_PID"
touch "$CONTINUE"
wait_marker "$DRAINED"
start_provider api-b "$API_B" "$ROUTE_B" "$DEALER_B" "$HTTP_B"
API_B_PID="$LAST_PID"
touch "$RESTORE"
wait "$B2_CLIENT_PID"
grep -q "scenario RL-B2 passed" "$LOG_DIR/client-rl-b2.stdout.log"
echo "scenario RL-B2 passed"
crash_provider "$HTTP_B" "$API_B_PID"
wait_registry_topology "api-b" "Ready" 0
post_consumer_new_client_burst "fast" "rl-c2-after-crash-" 8 "api-a"
wait_provider_evidence_value_prefix "rl-c2-after-crash-" "api-a"
start_provider api-b "$API_B" "$ROUTE_B" "$DEALER_B" "$HTTP_B"
API_B_PID="$LAST_PID"
sleep 1
post_consumer_profile_burst "fast" "rl-c2-restored-" 40
wait_provider_evidence_value_prefix "rl-c2-restored-" "api-b"
echo "scenario RL-C2 passed"
stop_pid "$API_B_PID"
stop_pid "$API_A_PID"

start_provider api-a "$API_A" "$ROUTE_A" "$DEALER_A" "$HTTP_A"
API_A_PID="$LAST_PID"
start_provider api-b "$API_B" "$ROUTE_B" "$DEALER_B" "$HTTP_B"
API_B_PID="$LAST_PID"
READY="$LOG_DIR/rl-a4-ready"
CONTINUE="$LOG_DIR/rl-a4-green-ready"
DRAINED="$LOG_DIR/rl-a4-green-stopped"
RESTORE="$LOG_DIR/rl-a4-restored"
run_client rl-a4 rl-a4 env \
  ZLINK_CPP_E2E_READY_FILE="$READY" \
  ZLINK_CPP_E2E_CONTINUE_FILE="$CONTINUE" \
  ZLINK_CPP_E2E_DRAINED_FILE="$DRAINED" \
  ZLINK_CPP_E2E_RESTORE_FILE="$RESTORE" &
A4_CLIENT_PID="$!"
wait_marker "$READY"
start_provider api-b "$API_B_GREEN" "$ROUTE_B_GREEN" "" "$HTTP_B_GREEN" "api-b-green"
API_B_GREEN_PID="$LAST_PID"
wait_registry_topology api-b Ready 2
touch "$CONTINUE"
wait_marker "$DRAINED"
wait "$API_B_GREEN_PID" >/dev/null 2>&1 || true
wait "$API_B_PID" >/dev/null 2>&1 || true
start_provider api-b "$API_B" "$ROUTE_B" "$DEALER_B" "$HTTP_B"
API_B_PID="$LAST_PID"
touch "$RESTORE"
wait "$A4_CLIENT_PID"
grep -q "scenario RL-A4 passed" "$LOG_DIR/client-rl-a4.stdout.log"
echo "scenario RL-A4 passed"
stop_pid "$API_B_PID"
stop_pid "$API_A_PID"

start_provider api-a "$API_A" "$ROUTE_A" "$DEALER_A" "$HTTP_A"
API_A_PID="$LAST_PID"
start_provider api-b "$API_B" "$ROUTE_B" "$DEALER_B" "$HTTP_B"
API_B_PID="$LAST_PID"
run_client rl-b4 rl-b4 env
grep -q "scenario RL-B4 passed" "$LOG_DIR/client-rl-b4.stdout.log"
echo "scenario RL-B4 passed"
run_client rl-b5 rl-b5 env
grep -q "scenario RL-B5 passed" "$LOG_DIR/client-rl-b5.stdout.log"
echo "scenario RL-B5 passed"
run_client rl-b6 rl-b6 env
grep -q "scenario RL-B6 passed" "$LOG_DIR/client-rl-b6.stdout.log"
echo "scenario RL-B6 passed"
stop_pid "$API_B_PID"
stop_pid "$API_A_PID"

start_provider api-a "$API_A" "$ROUTE_A" "$DEALER_A" "$HTTP_A"
API_A_PID="$LAST_PID"
start_provider api-b "$API_B" "$ROUTE_B" "$DEALER_B" "$HTTP_B"
API_B_PID="$LAST_PID"
wait_registry_topology api-b Ready 1
run_client rl-a3 rl-a3 env
grep -q "scenario RL-A3 passed" "$LOG_DIR/client-rl-a3.stdout.log"
echo "scenario RL-A3 passed"
run_client rl-c1 rl-c1 env
grep -q "scenario RL-C1 passed" "$LOG_DIR/client-rl-c1.stdout.log"
echo "scenario RL-C1 passed"
for index in 1 2 3; do
  stop_pid "$API_B_PID"
  run_client rl-a5 "rl-a5-down-$index" env \
    ZLINK_CPP_E2E_FLAP_PHASE=down \
    ZLINK_CPP_E2E_FLAP_CYCLE="$index"
  grep -q "scenario RL-A5 down passed" "$LOG_DIR/client-rl-a5-down-$index.stdout.log"
  start_provider api-b "$API_B" "$ROUTE_B" "$DEALER_B" "$HTTP_B"
  API_B_PID="$LAST_PID"
  wait_registry_topology api-b Ready 1
  run_client rl-a5 "rl-a5-up-$index" env \
    ZLINK_CPP_E2E_FLAP_PHASE=up \
    ZLINK_CPP_E2E_FLAP_CYCLE="$index"
  grep -q "scenario RL-A5 up passed" "$LOG_DIR/client-rl-a5-up-$index.stdout.log"
done
echo "scenario RL-A5 passed"
READY="$LOG_DIR/rl-c3-ready"
CONTINUE="$LOG_DIR/rl-c3-continue"
run_client rl-c3 rl-c3 env \
  ZLINK_CPP_E2E_READY_FILE="$READY" \
  ZLINK_CPP_E2E_CONTINUE_FILE="$CONTINUE" &
C3_CLIENT_PID="$!"
wait_marker "$READY"
wait "$API_B_PID" >/dev/null 2>&1 || true
start_provider api-b "$API_B" "$ROUTE_B" "$DEALER_B" "$HTTP_B"
API_B_PID="$LAST_PID"
touch "$CONTINUE"
wait "$C3_CLIENT_PID"
grep -q "scenario RL-C3 passed" "$LOG_DIR/client-rl-c3.stdout.log"
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
stop_pid "$API_A_PID"
start_provider api-a "$API_A" "$ROUTE_A" "$DEALER_A" "$HTTP_A"
API_A_PID="$LAST_PID"
sleep 5
run_client registry-recovered rl-c4-recovered env
grep -q "scenario RL-C4 recovery passed" "$LOG_DIR/client-rl-c4-recovered.stdout.log"
echo "scenario RL-C4 passed"
stop_pid "$API_B_PID"
stop_pid "$API_A_PID"

start_provider api-a "$API_A" "$ROUTE_A" "$DEALER_A" "$HTTP_A"
API_A_PID="$LAST_PID"
start_provider api-b "$API_B" "$ROUTE_B" "$DEALER_B" "$HTTP_B"
API_B_PID="$LAST_PID"
sleep 1
post_consumer_profile_burst "fast" "rl-d1-" 120
wait_provider_evidence_value_prefix "rl-d1-"
echo "scenario RL-D1 passed"
run_client rl-d4 rl-d4 env
grep -q "scenario RL-D4 passed" "$LOG_DIR/client-rl-d4.stdout.log"
echo "scenario RL-D4 passed"
run_client rl-d5 rl-d5 env
grep -q "scenario RL-D5 passed" "$LOG_DIR/client-rl-d5.stdout.log"
echo "scenario RL-D5 passed"
stop_pid "$API_B_PID"
stop_pid "$API_A_PID"

echo "resilience-lifecycle e2e result=passed"
