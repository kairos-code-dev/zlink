#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
JAVA_ROOT="$(cd "$ROOT_DIR/.." && pwd)"
SCENARIO="${1:-$ROOT_DIR/scenarios/CH-001.request-response.json}"
SCENARIO_ID="$(python3 - "$SCENARIO" <<'PY'
import json
import sys
from pathlib import Path
print(json.loads(Path(sys.argv[1]).read_text())["id"])
PY
)"
WORK_DIR="${ZLINK_SCENARIO_E2E_WORK_DIR:-$ROOT_DIR/build/scenario-e2e-$SCENARIO_ID}"
LOG_DIR="$WORK_DIR/logs"
REPORT_FILE="$WORK_DIR/report.json"
APP_BIN="$ROOT_DIR/build/install/zlink-framework-kotlin-scenario-e2e/bin/zlink-framework-kotlin-scenario-e2e"
SCRIPT_PATH="$(realpath "$0")"

mkdir -p "$LOG_DIR"
rm -f "$WORK_DIR"/*.ready "$REPORT_FILE"

pick_port() {
  python3 - "$@" <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
}

CLIENT_ZLINK_ENDPOINT=""
ROUTE_PEER_ENDPOINTS=""

if [[ "$SCENARIO_ID" == "CH-002" ]]; then
  SERVER_NAMES=("api-a" "api-b" "api-c")
elif [[ "$SCENARIO_ID" == "CH-004" ]]; then
  SERVER_NAMES=("peer-b" "peer-c")
else
  SERVER_NAMES=("api-a")
fi

SERVER_PIDS=()
ZLINK_ENDPOINTS=()
HTTP_ENDPOINTS=()
SERVER_PROCESS_IDS=()

cleanup() {
  for pid in "${SERVER_PIDS[@]:-}"; do
    if kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
    fi
  done
  sleep 0.2
  for pid in "${SERVER_PIDS[@]:-}"; do
    if kill -0 "$pid" 2>/dev/null; then
      kill -9 "$pid" 2>/dev/null || true
    fi
  done
  for pid in "${SERVER_PIDS[@]:-}"; do
    wait "$pid" 2>/dev/null || true
  done
}
trap cleanup EXIT

(cd "$JAVA_ROOT" && ./gradlew :zlink-framework-kotlin-scenario-e2e:installDist >/dev/null)

if [[ -f "$JAVA_ROOT/../../../core/build/lib/libzlink.so" ]]; then
  export ZLINK_LIBRARY_PATH="$JAVA_ROOT/../../../core/build/lib/libzlink.so"
fi

if [[ "$SCENARIO_ID" == "CH-004" ]]; then
  CLIENT_ZLINK_ENDPOINT="tcp://127.0.0.1:$(pick_port)"
  peer_b_endpoint="tcp://127.0.0.1:$(pick_port)"
  peer_c_endpoint="tcp://127.0.0.1:$(pick_port)"
fi

for server_name in "${SERVER_NAMES[@]}"; do
  if [[ "$SCENARIO_ID" == "CH-004" && "$server_name" == "peer-b" ]]; then
    zlink_endpoint="$peer_b_endpoint"
    route_peer_endpoints="$CLIENT_ZLINK_ENDPOINT,$peer_c_endpoint"
  elif [[ "$SCENARIO_ID" == "CH-004" && "$server_name" == "peer-c" ]]; then
    zlink_endpoint="$peer_c_endpoint"
    route_peer_endpoints="$CLIENT_ZLINK_ENDPOINT,$peer_b_endpoint"
  else
    zlink_endpoint="tcp://127.0.0.1:$(pick_port)"
    route_peer_endpoints=""
  fi
  http_endpoint="http://127.0.0.1:$(pick_port)"
  ready_file="$WORK_DIR/$server_name.ready"
  ZLINK_ENDPOINTS+=("$zlink_endpoint")
  HTTP_ENDPOINTS+=("$http_endpoint")

  "$APP_BIN" server \
    --work-dir "$WORK_DIR" \
    --server-name "$server_name" \
    --zlink-endpoint "$zlink_endpoint" \
    --http-endpoint "$http_endpoint" \
    --ready-file "$ready_file" \
    --mode "$([[ "$SCENARIO_ID" == "CH-004" ]] && echo route-peer || echo channel)" \
    --route-peer-endpoints "$route_peer_endpoints" \
    >"$LOG_DIR/server-$server_name.log" 2>&1 &
  SERVER_PIDS+=("$!")
  SERVER_PROCESS_IDS+=("server:$server_name=${SERVER_PIDS[-1]}")
done

for index in "${!SERVER_NAMES[@]}"; do
  server_name="${SERVER_NAMES[$index]}"
  server_pid="${SERVER_PIDS[$index]}"
  ready_file="$WORK_DIR/$server_name.ready"
  for _ in {1..100}; do
    if [[ -f "$ready_file" ]]; then
      break
    fi
    if ! kill -0 "$server_pid" 2>/dev/null; then
      echo "server process exited before readiness: $server_name" >&2
      tail -100 "$LOG_DIR/server-$server_name.log" >&2 || true
      exit 1
    fi
    sleep 0.1
  done

  if [[ ! -f "$ready_file" ]]; then
    echo "server readiness timed out: $server_name" >&2
    tail -100 "$LOG_DIR/server-$server_name.log" >&2 || true
    exit 1
  fi
done

if [[ "$SCENARIO_ID" == "CH-004" ]]; then
  ZLINK_ENDPOINT="$CLIENT_ZLINK_ENDPOINT"
  ROUTE_PEER_ENDPOINTS="$(IFS=,; echo "${ZLINK_ENDPOINTS[*]}")"
else
  ZLINK_ENDPOINT="$(IFS=,; echo "${ZLINK_ENDPOINTS[*]}")"
fi
HTTP_ENDPOINT="$(IFS=,; echo "${HTTP_ENDPOINTS[*]}")"
SERVER_PROCESS_IDS_ARG="$(IFS=,; echo "${SERVER_PROCESS_IDS[*]}")"

"$APP_BIN" client \
  --work-dir "$WORK_DIR" \
  --scenario "$SCENARIO" \
  --zlink-endpoint "$ZLINK_ENDPOINT" \
  --http-endpoint "$HTTP_ENDPOINT" \
  --mode "$([[ "$SCENARIO_ID" == "CH-004" ]] && echo route-peer || echo channel)" \
  --route-peer-endpoints "$ROUTE_PEER_ENDPOINTS" \
  --client-routing-id "$([[ "$SCENARIO_ID" == "CH-004" ]] && echo peer-a || echo client-a)" \
  --report-file "$REPORT_FILE" \
  --log-dir "$LOG_DIR" \
  --script-path "$SCRIPT_PATH" \
  --server-process-ids "$SERVER_PROCESS_IDS_ARG" \
  >"$LOG_DIR/client.log" 2>&1

if [[ ! -f "$REPORT_FILE" ]]; then
  echo "client did not write report: $REPORT_FILE" >&2
  tail -100 "$LOG_DIR/client.log" >&2 || true
  exit 1
fi

python3 - "$REPORT_FILE" "$SCENARIO" "$LOG_DIR" "$HTTP_ENDPOINT" "${#SERVER_NAMES[@]}" "$SCRIPT_PATH" "$SERVER_PROCESS_IDS_ARG" <<'PY'
import json
import sys
from pathlib import Path

report_path = Path(sys.argv[1])
scenario_path = Path(sys.argv[2])
log_dir = Path(sys.argv[3])
http_endpoints = sys.argv[4].split(",")
server_count = int(sys.argv[5])
script_path = Path(sys.argv[6])
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
        raise SystemExit(f"report client process id must be a positive integer for {key}: {report}")
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
expected_evidence = ",".join(f"{endpoint}/evidence" for endpoint in http_endpoints)
if report["evidencePath"] != expected_evidence:
    raise SystemExit(f"report evidence path does not match runner endpoint: {report}")
if scenario["artifacts"]["evidence"] not in ("server:/evidence", "servers:/evidence"):
    raise SystemExit(f"unsupported scenario evidence artifact: {scenario['artifacts']['evidence']}")
PY

echo "scenario-e2e result=passed scenario=$SCENARIO report=$REPORT_FILE logs=$LOG_DIR"
