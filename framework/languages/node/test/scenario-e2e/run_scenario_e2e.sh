#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NODE_ROOT="$(cd "$ROOT_DIR/../.." && pwd)"
RUNNER="$ROOT_DIR/scenario-e2e.js"
SCENARIO="${1:-$ROOT_DIR/scenarios/CH-001.request-response.json}"
WORK_DIR="${ZLINK_SCENARIO_E2E_WORK_DIR:-$ROOT_DIR/.scenario-e2e}"
LOG_DIR="$WORK_DIR/logs"
READY_FILE="$WORK_DIR/server.ready"
REPORT_FILE="$WORK_DIR/report.json"
SCENARIO_ID="$(python3 - "$SCENARIO" <<'PY'
import json
import sys
from pathlib import Path
print(json.loads(Path(sys.argv[1]).read_text())["id"])
PY
)"

rm -rf "$WORK_DIR"
mkdir -p "$LOG_DIR"

pick_port() {
  python3 - "$@" <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
}

SERVER_PIDS=()
SERVER_NAMES=()
READY_FILES=()
ZLINK_ENDPOINTS=()
HTTP_ENDPOINTS=()
CLIENT_ZLINK_ENDPOINT=""

cleanup() {
  for pid in "${SERVER_PIDS[@]}"; do
    if kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
    fi
  done
  sleep 0.2
  for pid in "${SERVER_PIDS[@]}"; do
    if kill -0 "$pid" 2>/dev/null; then
      kill -9 "$pid" 2>/dev/null || true
    fi
    wait "$pid" 2>/dev/null || true
  done
}
trap cleanup EXIT

npm --prefix "$NODE_ROOT" run build >/dev/null

start_server() {
  local server_name="$1"
  local mode="${2:-channel-server}"
  local route_peers="${3:-}"
  local fixed_zlink_endpoint="${4:-}"
  local zlink_port
  local http_port
  local zlink_endpoint
  local http_endpoint
  local ready_file
  local pid

  http_port="$(pick_port)"
  if [[ -n "$fixed_zlink_endpoint" ]]; then
    zlink_endpoint="$fixed_zlink_endpoint"
  else
    zlink_port="$(pick_port)"
    zlink_endpoint="tcp://127.0.0.1:$zlink_port"
  fi
  http_endpoint="http://127.0.0.1:$http_port"
  ready_file="$WORK_DIR/$server_name.ready"

  ZLINK_ENDPOINTS+=("$zlink_endpoint")
  HTTP_ENDPOINTS+=("$http_endpoint")
  READY_FILES+=("$ready_file")

  node "$RUNNER" server \
    --work-dir "$WORK_DIR" \
    --zlink-endpoint "$zlink_endpoint" \
    --http-endpoint "$http_endpoint" \
    --ready-file "$ready_file" \
    --server-name "$server_name" \
    --mode "$mode" \
    --route-peer-endpoints "$route_peers" \
    >"$LOG_DIR/server-$server_name.log" 2>&1 &
  pid="$!"
  SERVER_PIDS+=("$pid")
  SERVER_NAMES+=("$server_name")
}

if [[ "$SCENARIO_ID" == "CH-004" ]]; then
  CLIENT_ZLINK_ENDPOINT="tcp://127.0.0.1:$(pick_port)"
  peer_b_endpoint="tcp://127.0.0.1:$(pick_port)"
  peer_c_endpoint="tcp://127.0.0.1:$(pick_port)"
  start_server "peer-b" "route-peer" "$CLIENT_ZLINK_ENDPOINT,$peer_c_endpoint" "$peer_b_endpoint"
  start_server "peer-c" "route-peer" "$CLIENT_ZLINK_ENDPOINT,$peer_b_endpoint" "$peer_c_endpoint"
elif [[ "$SCENARIO_ID" == "CH-002" ]]; then
  start_server "api-a"
  start_server "api-b"
  start_server "api-c"
else
  start_server "api-a"
fi

for _ in {1..100}; do
  ready_count=0
  for ready_file in "${READY_FILES[@]}"; do
    [[ -f "$ready_file" ]] && ready_count=$((ready_count + 1))
  done
  [[ "$ready_count" -eq "${#READY_FILES[@]}" ]] && break
  for index in "${!SERVER_PIDS[@]}"; do
    if ! kill -0 "${SERVER_PIDS[$index]}" 2>/dev/null; then
      echo "server process exited before readiness" >&2
      tail -100 "$LOG_DIR"/server-*.log >&2 || true
      exit 1
    fi
  done
  sleep 0.1
done

ready_count=0
for ready_file in "${READY_FILES[@]}"; do
  [[ -f "$ready_file" ]] && ready_count=$((ready_count + 1))
done
if [[ "$ready_count" -ne "${#READY_FILES[@]}" ]]; then
  echo "server readiness timed out" >&2
  tail -100 "$LOG_DIR"/server-*.log >&2 || true
  exit 1
fi

join_by_comma() {
  local IFS=,
  echo "$*"
}

ZLINK_ENDPOINT="$(join_by_comma "${ZLINK_ENDPOINTS[@]}")"
ROUTE_PEER_ENDPOINTS=""
if [[ "$SCENARIO_ID" == "CH-004" ]]; then
  ROUTE_PEER_ENDPOINTS="$ZLINK_ENDPOINT"
  ZLINK_ENDPOINT="$CLIENT_ZLINK_ENDPOINT"
fi
HTTP_ENDPOINT="$(join_by_comma "${HTTP_ENDPOINTS[@]}")"
SERVER_PROCESS_IDS=()
for index in "${!SERVER_PIDS[@]}"; do
  SERVER_PROCESS_IDS+=("server:${SERVER_NAMES[$index]}=${SERVER_PIDS[$index]}")
done
SERVER_PROCESS_ID_ARG="$(join_by_comma "${SERVER_PROCESS_IDS[@]}")"

node "$RUNNER" client \
  --work-dir "$WORK_DIR" \
  --scenario "$SCENARIO" \
  --zlink-endpoint "$ZLINK_ENDPOINT" \
  --http-endpoint "$HTTP_ENDPOINT" \
  --route-peer-endpoints "$ROUTE_PEER_ENDPOINTS" \
  --report-file "$REPORT_FILE" \
  --log-dir "$LOG_DIR" \
  --executed-script "$ROOT_DIR/run_scenario_e2e.sh" \
  --server-process-ids "$SERVER_PROCESS_ID_ARG" \
  >"$LOG_DIR/client.log" 2>&1

if [[ ! -f "$REPORT_FILE" ]]; then
  echo "client did not write report: $REPORT_FILE" >&2
  tail -100 "$LOG_DIR/client.log" >&2 || true
  exit 1
fi

python3 - "$REPORT_FILE" "$SCENARIO" "$LOG_DIR" "$HTTP_ENDPOINT" "$ROOT_DIR/run_scenario_e2e.sh" "${#SERVER_NAMES[@]}" "$SERVER_PROCESS_ID_ARG" <<'PY'
import json
import sys
from pathlib import Path

report_path = Path(sys.argv[1])
scenario_path = Path(sys.argv[2])
log_dir = Path(sys.argv[3])
http_endpoints = [item for item in sys.argv[4].split(",") if item]
script_path = Path(sys.argv[5])
server_count = int(sys.argv[6])
expected_server_pids = dict(entry.split("=", 1) for entry in sys.argv[7].split(",") if entry)
report = json.loads(report_path.read_text())
scenario = json.loads(scenario_path.read_text())
required = [
    "scenarioId",
    "language",
    "runtimeVersion",
    "transportBackend",
    "codec",
    "processCount",
    "passed",
    "failed",
    "skipped",
    "evidencePath",
    "logPath",
    "executedScriptPath",
    "processIds",
]
missing = [name for name in required if name not in report or report[name] in ("", None)]
if missing:
    raise SystemExit(f"report missing required fields: {', '.join(missing)}")
if "failureLayer" not in report:
    raise SystemExit(f"report missing failureLayer field: {report}")
if report["result"] != "passed" or report["passed"] < 1 or report["failed"] != 0:
    raise SystemExit(f"scenario did not pass: {report}")
if report["processCount"] != server_count + 1:
    raise SystemExit(f"unexpected process count: {report}")
if Path(report["executedScriptPath"]).resolve() != script_path.resolve():
    raise SystemExit(f"report script path does not match runner: {report}")
if len(report["processIds"]) != server_count + 1:
    raise SystemExit(f"report process ids do not include every server and client process: {report}")
for client in scenario["roles"]["client"]:
    key = f"client:{client['name']}"
    if key not in report["processIds"]:
        raise SystemExit(f"report process ids missing {key}: {report}")
    if not isinstance(report["processIds"][key], int) or report["processIds"][key] <= 0:
        raise SystemExit(f"report client process id for {key} must be a positive integer: {report}")
for name in scenario["roles"]["server"]:
    key = f"server:{name['name']}"
    if key not in report["processIds"]:
        raise SystemExit(f"report process ids missing {key}: {report}")
    if str(report["processIds"][key]) != expected_server_pids[key]:
        raise SystemExit(f"report server process id does not match runner pid for {key}: {report}")
if report["scenarioId"] != scenario["id"]:
    raise SystemExit(f"report scenario id does not match scenario file: {report}")
if Path(report["scenarioFile"]).resolve() != scenario_path.resolve():
    raise SystemExit(f"report scenario file does not match executed scenario: {report}")
if Path(report["logPath"]).resolve() != log_dir.resolve():
    raise SystemExit(f"report log path does not match runner log directory: {report}")
if Path(report_path).resolve() != (log_dir.parent / scenario["artifacts"]["report"]).resolve():
    raise SystemExit(f"report file does not match scenario artifacts.report: {report}")
if Path(report["logPath"]).name != scenario["artifacts"]["logs"]:
    raise SystemExit(f"report log path does not match scenario artifacts.logs: {report}")
expected_evidence_path = ",".join(f"{endpoint}/evidence" for endpoint in http_endpoints)
if report["evidencePath"] != expected_evidence_path:
    raise SystemExit(f"report evidence path does not match runner endpoint: {report}")
if scenario["artifacts"]["evidence"] not in ("server:/evidence", "servers:/evidence"):
    raise SystemExit(f"unsupported scenario evidence artifact: {scenario['artifacts']['evidence']}")
if scenario["id"] == "DERR-001":
    marker = "zlink node scenario dispatch error: surface=channel kind=request reason=handlerMissing action=replyError packetName=MissingScenarioRequest channelName=scenario-api"
    logs = [(log_dir / f"server-{server['name']}.log").read_text() for server in scenario["roles"]["server"]]
    if not any(marker in text for text in logs):
        raise SystemExit(f"DERR-001 server log marker missing: {logs}")
if scenario["id"] == "DERR-002":
    marker = "zlink node scenario dispatch error: surface=channel kind=send reason=handlerMissing action=drop packetName=MissingScenarioCommand channelName=scenario-api"
    logs = [(log_dir / f"server-{server['name']}.log").read_text() for server in scenario["roles"]["server"]]
    if not any(marker in text for text in logs):
        raise SystemExit(f"DERR-002 server log marker missing: {logs}")
if scenario["id"] == "DERR-007":
    marker = "zlink node scenario dispatch error: surface=channel kind=request reason=handlerException action=replyError packetName=ScenarioProfileRequest channelName=scenario-api error=DERR-007 handler exception"
    logs = [(log_dir / f"server-{server['name']}.log").read_text() for server in scenario["roles"]["server"]]
    if not any(marker in text for text in logs):
        raise SystemExit(f"DERR-007 server log marker missing: {logs}")
PY

echo "scenario-e2e result=passed scenario=$SCENARIO report=$REPORT_FILE logs=$LOG_DIR"
