#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${ZLINK_CPP_BUILD_DIR:-$CPP_ROOT/build}"
BINARY="$BUILD_DIR/test_cpp_framework_scenario_e2e"
SCENARIO="${1:-$SCRIPT_DIR/scenarios/CH-001.request-response.json}"
CHANNEL_NAME="scenario.api"
SCENARIO_ID="$(python3 - "$SCENARIO" <<'PY'
import json
import sys
from pathlib import Path
print(json.loads(Path(sys.argv[1]).read_text())["id"])
PY
)"
WORK_DIR="${ZLINK_SCENARIO_E2E_WORK_DIR:-$SCRIPT_DIR/build/scenario-e2e-$SCENARIO_ID}"
LOG_DIR="$WORK_DIR/logs"
REPORT_FILE="$WORK_DIR/report.json"
CLIENT_READY_FILE="$WORK_DIR/client.ready"

cmake --build "$BUILD_DIR" --target test_cpp_framework_scenario_e2e -j2

rm -rf "$WORK_DIR"
mkdir -p "$LOG_DIR"

pick_port() {
  python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
}

server_pids=()
server_names=()
ready_files=()
zlink_endpoints=()
http_endpoints=()
CLIENT_MODE="client-server"
CLIENT_ZLINK_ENDPOINT=""
CLIENT_ROUTE_PEER_ENDPOINTS=""

cleanup() {
  for pid in "${server_pids[@]}"; do
    if kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
    fi
  done
  sleep 0.2
  for pid in "${server_pids[@]}"; do
    if kill -0 "$pid" 2>/dev/null; then
      kill -9 "$pid" 2>/dev/null || true
    fi
    wait "$pid" 2>/dev/null || true
  done
}
trap cleanup EXIT

make_tcp_endpoint() {
  local port
  port="$(pick_port)"
  echo "tcp://127.0.0.1:$port"
}

make_http_endpoint() {
  local port
  port="$(pick_port)"
  echo "http://127.0.0.1:$port"
}

start_server() {
  local server_name="$1"
  local mode="${2:-client-server}"
  local zlink_endpoint="${3:-}"
  local http_endpoint="${4:-}"
  local route_peer_endpoints="${5:-}"
  local ready_file
  local pid

  if [[ -z "$zlink_endpoint" ]]; then
    zlink_endpoint="$(make_tcp_endpoint)"
  fi
  if [[ -z "$http_endpoint" ]]; then
    http_endpoint="$(make_http_endpoint)"
  fi
  ready_file="$WORK_DIR/$server_name.ready"

  zlink_endpoints+=("$zlink_endpoint")
  http_endpoints+=("$http_endpoint")
  ready_files+=("$ready_file")

  "$BINARY" server \
    --scenario "$SCENARIO" \
    --work-dir "$WORK_DIR" \
    --log-dir "$LOG_DIR" \
    --report "$REPORT_FILE" \
    --ready-file "$ready_file" \
    --zlink-endpoint "$zlink_endpoint" \
    --http-endpoint "$http_endpoint" \
    --channel "$CHANNEL_NAME" \
    --server-name "$server_name" \
    --mode "$mode" \
    --route-peer-endpoints "$route_peer_endpoints" \
    >"$LOG_DIR/server-$server_name.log" 2>"$LOG_DIR/server-$server_name.err.log" &
  pid="$!"
  server_pids+=("$pid")
  server_names+=("$server_name")
}

if [[ "$SCENARIO_ID" == "CH-004" ]]; then
  peer_a_zlink="$(make_tcp_endpoint)"
  peer_b_zlink="$(make_tcp_endpoint)"
  peer_c_zlink="$(make_tcp_endpoint)"
  peer_b_http="$(make_http_endpoint)"
  peer_c_http="$(make_http_endpoint)"
  start_server "peer-b" "route-peer" "$peer_b_zlink" "$peer_b_http" "$peer_a_zlink,$peer_c_zlink"
  start_server "peer-c" "route-peer" "$peer_c_zlink" "$peer_c_http" "$peer_a_zlink,$peer_b_zlink"
  CLIENT_MODE="route-peer"
  CLIENT_ZLINK_ENDPOINT="$peer_a_zlink"
  CLIENT_ROUTE_PEER_ENDPOINTS="$peer_b_zlink,$peer_c_zlink"
elif [[ "$SCENARIO_ID" == "CH-002" ]]; then
  start_server "api-a"
  start_server "api-b"
  start_server "api-c"
else
  start_server "api-a"
fi

for _ in $(seq 1 100); do
  ready_count=0
  for ready_file in "${ready_files[@]}"; do
    [[ -f "$ready_file" ]] && ready_count=$((ready_count + 1))
  done
  [[ "$ready_count" -eq "${#ready_files[@]}" ]] && break
  for index in "${!server_pids[@]}"; do
    if ! kill -0 "${server_pids[$index]}" 2>/dev/null; then
      echo "scenario-e2e server exited before readiness" >&2
      cat "$LOG_DIR"/server-*.err.log >&2 || true
      exit 1
    fi
  done
  sleep 0.1
done

ready_count=0
for ready_file in "${ready_files[@]}"; do
  [[ -f "$ready_file" ]] && ready_count=$((ready_count + 1))
done
if [[ "$ready_count" -ne "${#ready_files[@]}" ]]; then
  echo "scenario-e2e server readiness timed out" >&2
  cat "$LOG_DIR"/server-*.err.log >&2 || true
  exit 1
fi

join_by_comma() {
  local IFS=,
  echo "$*"
}

ZLINK_ENDPOINT="$(join_by_comma "${zlink_endpoints[@]}")"
if [[ -z "$CLIENT_ZLINK_ENDPOINT" ]]; then
  CLIENT_ZLINK_ENDPOINT="$ZLINK_ENDPOINT"
fi
HTTP_ENDPOINT="$(join_by_comma "${http_endpoints[@]}")"
server_pid_entries=()
for index in "${!server_pids[@]}"; do
  server_pid_entries+=("${server_names[$index]}=${server_pids[$index]}")
done
SERVER_PIDS="$(join_by_comma "${server_pid_entries[@]}")"

"$BINARY" client \
  --scenario "$SCENARIO" \
  --work-dir "$WORK_DIR" \
  --log-dir "$LOG_DIR" \
  --report "$REPORT_FILE" \
  --ready-file "$CLIENT_READY_FILE" \
  --zlink-endpoint "$CLIENT_ZLINK_ENDPOINT" \
  --http-endpoint "$HTTP_ENDPOINT" \
  --channel "$CHANNEL_NAME" \
  --mode "$CLIENT_MODE" \
  --route-peer-endpoints "$CLIENT_ROUTE_PEER_ENDPOINTS" \
  --executed-script "$SCRIPT_DIR/run_scenario_e2e.sh" \
  --server-pids "$SERVER_PIDS" \
  >"$LOG_DIR/client.log" 2>"$LOG_DIR/client.err.log"

python3 - "$REPORT_FILE" "$SCENARIO" "$LOG_DIR" "$SCRIPT_DIR/run_scenario_e2e.sh" "$HTTP_ENDPOINT" "$SERVER_PIDS" <<'PY'
import json
import pathlib
import sys

report_path = pathlib.Path(sys.argv[1])
scenario_path = pathlib.Path(sys.argv[2])
log_dir = pathlib.Path(sys.argv[3])
script_path = pathlib.Path(sys.argv[4])
http_endpoints = [item for item in sys.argv[5].split(",") if item]
expected_server_pids = {
    "server:" + name: pid
    for name, pid in (entry.split("=", 1) for entry in sys.argv[6].split(",") if entry)
}

report = json.loads(report_path.read_text())
scenario = json.loads(scenario_path.read_text())
required = [
    "scenarioId",
    "language",
    "runtimeVersion",
    "transportBackend",
    "codec",
    "result",
    "scenarioFile",
    "executedScriptPath",
    "processCount",
    "processIds",
    "passed",
    "failed",
    "skipped",
    "logPath",
    "evidencePath",
    "failureLayer",
]
missing = [name for name in required if name not in report]
assert not missing, (missing, report)
assert report["scenarioId"] == scenario["id"], report
assert report["language"] == "cpp", report
assert report["result"] == "passed", report
assert pathlib.Path(report["scenarioFile"]).resolve() == scenario_path.resolve()
assert pathlib.Path(report["executedScriptPath"]).resolve() == script_path.resolve()
assert pathlib.Path(report["logPath"]).resolve() == log_dir.resolve()
assert report["evidencePath"] == ",".join(endpoint + "/evidence" for endpoint in http_endpoints), report
expected_process_count = len(scenario["roles"]["server"]) + len(scenario["roles"]["client"])
assert report["processCount"] == expected_process_count, report
assert len(report["processIds"]) == report["processCount"], report
assert all(str(key).startswith(("server:", "client:")) for key in report["processIds"].keys()), report
client_key = "client:" + scenario["roles"]["client"][0]["name"]
assert isinstance(report["processIds"].get(client_key), int), report
assert report["processIds"][client_key] > 0, report
for server in scenario["roles"]["server"]:
    key = "server:" + server["name"]
    assert key in report["processIds"], report
    assert str(report["processIds"][key]) == expected_server_pids[key], report
if scenario["id"] == "CH-007":
    marker = "zlink framework channel late reply ignored"
    logs = []
    for server in scenario["roles"]["server"]:
        logs.append((log_dir / ("server-" + server["name"] + ".err.log")).read_text())
    assert any(marker in text for text in logs), logs
if scenario["id"] == "DERR-001":
    marker = "zlink framework dispatch error: surface=channel kind=request reason=handler_missing action=reply_error packet=MissingScenarioRequest channel=scenario.api"
    logs = []
    for server in scenario["roles"]["server"]:
        logs.append((log_dir / ("server-" + server["name"] + ".err.log")).read_text())
    assert any(marker in text for text in logs), logs
if scenario["id"] == "DERR-002":
    marker = "zlink framework dispatch error: surface=channel kind=send reason=handler_missing action=drop packet=MissingScenarioCommand channel=scenario.api"
    logs = []
    for server in scenario["roles"]["server"]:
        logs.append((log_dir / ("server-" + server["name"] + ".err.log")).read_text())
    assert any(marker in text for text in logs), logs
if scenario["id"] == "DERR-007":
    marker = "zlink framework dispatch error: surface=channel kind=request reason=handler_exception action=reply_error packet=ScenarioProfileRequest channel=scenario.api"
    logs = []
    for server in scenario["roles"]["server"]:
        logs.append((log_dir / ("server-" + server["name"] + ".err.log")).read_text())
    assert any(marker in text for text in logs), logs
assert report["passed"] == 1, report
assert report["failed"] == 0, report
assert report["skipped"] == 0, report
assert report["failureLayer"] is None, report
PY

echo "scenario-e2e result=passed scenario=$SCENARIO report=$REPORT_FILE logs=$LOG_DIR"
