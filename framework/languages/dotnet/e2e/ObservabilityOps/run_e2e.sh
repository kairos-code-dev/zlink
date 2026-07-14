#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODE="${1:-all}"
source "$ROOT_DIR/../redis-common.sh"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
LOCAL_READINESS_TIMEOUT_SECONDS="${ZLINK_DOTNET_E2E_READY_TIMEOUT_SECONDS:-3}"
LOCAL_READINESS_POLL_SECONDS=0.1
LOCAL_READINESS_ATTEMPTS="$(
  python3 - "$LOCAL_READINESS_TIMEOUT_SECONDS" "$LOCAL_READINESS_POLL_SECONDS" <<'PY'
import math
import sys

print(max(1, math.ceil(float(sys.argv[1]) / float(sys.argv[2]))))
PY
)"
mkdir -p "$LOG_DIR"
export BINGO_LOG_DIR="$LOG_DIR"
export BINGO_REDIS_KEY_PREFIX="observability-ops:${RUN_ID}:"
export SHOPPINGMALL_LOG_DIR="$LOG_DIR"
export SHOPPINGMALL_REDIS_KEY_PREFIX="observability-ops:${RUN_ID}:shoppingmall:"

SERVER_PROJECT="$ROOT_DIR/Server/ObservabilityOps.Server.csproj"
TRIGGER_PROJECT="$ROOT_DIR/Trigger/ObservabilityOps.Trigger.csproj"
CLIENT_PROJECT="$ROOT_DIR/../../samples/Bingo/Client/Bingo.Client.csproj"
SERVER_DLL="$ROOT_DIR/Server/bin/Debug/net8.0/ObservabilityOps.Server.dll"
TRIGGER_DLL="$ROOT_DIR/Trigger/bin/Debug/net8.0/ObservabilityOps.Trigger.dll"
CLIENT_DLL="$ROOT_DIR/../../samples/Bingo/Client/bin/Debug/net8.0/Bingo.Client.dll"
PIDS=()
ROLE_PIDS=()
declare -A ROLE_PIDS_BY_NAME=()

stop_processes() {
  local -a processes=("$@")
  local pid
  for pid in "${processes[@]}"; do kill -TERM "$pid" >/dev/null 2>&1 || true; done
  for _ in $(seq 1 150); do
    local alive=0
    for pid in "${processes[@]}"; do
      if kill -0 "$pid" >/dev/null 2>&1; then alive=1; fi
    done
    if [[ "$alive" -eq 0 ]]; then break; fi
    sleep 0.1
  done
  for pid in "${processes[@]}"; do kill -KILL "$pid" >/dev/null 2>&1 || true; done
  for pid in "${processes[@]}"; do wait "$pid" 2>/dev/null || true; done
}

cleanup() {
  local code=$?
  stop_processes "${PIDS[@]}"
  if [[ -n "${REDIS_CONTAINER:-}" ]]; then docker rm -fv "$REDIS_CONTAINER" >/dev/null 2>&1 || true; fi
  if [[ "$code" -ne 0 ]]; then echo "ObservabilityOps failed. Logs: $LOG_DIR" >&2; fi
}
trap cleanup EXIT

read -r -a PORTS <<<"$(python3 - <<'PY'
import socket
sockets = []
try:
    for _ in range(30):
        sock = socket.socket()
        sock.bind(('127.0.0.1', 0))
        sockets.append(sock)
    print(' '.join(str(sock.getsockname()[1]) for sock in sockets))
finally:
    for sock in sockets:
        sock.close()
PY
)"

export BINGO_API_A_CHANNEL_ENDPOINT="tcp://127.0.0.1:${PORTS[0]}"
export BINGO_API_B_CHANNEL_ENDPOINT="tcp://127.0.0.1:${PORTS[1]}"
export BINGO_PLAY_A_CHANNEL_ENDPOINT="tcp://127.0.0.1:${PORTS[2]}"
export BINGO_PLAY_B_CHANNEL_ENDPOINT="tcp://127.0.0.1:${PORTS[3]}"
export BINGO_PLAY_A_SPOT_ENDPOINT="tcp://127.0.0.1:${PORTS[4]}"
export BINGO_PLAY_B_SPOT_ENDPOINT="tcp://127.0.0.1:${PORTS[5]}"
export BINGO_PLAY_A_SPOT_ROUTER_ENDPOINT="tcp://127.0.0.1:${PORTS[6]}"
export BINGO_PLAY_B_SPOT_ROUTER_ENDPOINT="tcp://127.0.0.1:${PORTS[7]}"
export BINGO_SESSION_A_SPOT_ENDPOINT="tcp://127.0.0.1:${PORTS[8]}"
export BINGO_SESSION_B_SPOT_ENDPOINT="tcp://127.0.0.1:${PORTS[9]}"
export BINGO_SESSION_A_ROUTER_ENDPOINT="tcp://127.0.0.1:${PORTS[10]}"
export BINGO_SESSION_B_ROUTER_ENDPOINT="tcp://127.0.0.1:${PORTS[11]}"
export BINGO_SESSION_A_STREAM_ENDPOINT="tcp://127.0.0.1:${PORTS[12]}"
export BINGO_SESSION_B_STREAM_ENDPOINT="tcp://127.0.0.1:${PORTS[13]}"
export SHOPPINGMALL_API_A_HTTP_URL="http://127.0.0.1:${PORTS[20]}"
export SHOPPINGMALL_API_B_HTTP_URL="http://127.0.0.1:${PORTS[21]}"
export SHOPPINGMALL_WORKFLOW_A_HTTP_URL="http://127.0.0.1:${PORTS[20]}"
export SHOPPINGMALL_WORKFLOW_B_HTTP_URL="http://127.0.0.1:${PORTS[21]}"
export SHOPPINGMALL_WORKFLOW_A_CHANNEL_ENDPOINT="tcp://127.0.0.1:${PORTS[22]}"
export SHOPPINGMALL_WORKFLOW_B_CHANNEL_ENDPOINT="tcp://127.0.0.1:${PORTS[23]}"
export SHOPPINGMALL_WORKFLOW_A_SPOT_ENDPOINT="tcp://127.0.0.1:${PORTS[24]}"
export SHOPPINGMALL_WORKFLOW_B_SPOT_ENDPOINT="tcp://127.0.0.1:${PORTS[25]}"
export SHOPPINGMALL_WORKFLOW_A_SPOT_ROUTER_ENDPOINT="tcp://127.0.0.1:${PORTS[26]}"
export SHOPPINGMALL_WORKFLOW_B_SPOT_ROUTER_ENDPOINT="tcp://127.0.0.1:${PORTS[27]}"

zlink_redis_start_scoped_assign \
  REDIS_CONTAINER REDIS_ENDPOINT \
  "zlink-dotnet-observability-ops-${RUN_ID}" \
  "redis:7.2-alpine" "$LOG_DIR"
export BINGO_REDIS_ENDPOINT="$REDIS_ENDPOINT"
export SHOPPINGMALL_REDIS_ENDPOINT="$REDIS_ENDPOINT"

dotnet build "$SERVER_PROJECT" --no-restore --maxcpucount:1 >/dev/null
dotnet build "$TRIGGER_PROJECT" --no-restore --maxcpucount:1 >/dev/null
dotnet build "$CLIENT_PROJECT" --no-restore --maxcpucount:1 >/dev/null

wait_health() {
  local url="$1"
  for _ in $(seq 1 "$LOCAL_READINESS_ATTEMPTS"); do
    if curl -fsS --max-time 1 "$url/health" >/dev/null 2>&1; then return 0; fi
    sleep "$LOCAL_READINESS_POLL_SECONDS"
  done
  echo "Timed out waiting ${LOCAL_READINESS_TIMEOUT_SECONDS}s for $url" >&2
  return 1
}

launch_role() {
  local role="$1"
  local -a metrics_args=()
  if [[ "$role" == "play-b" ]]; then metrics_args=(--metrics off); fi
  env ZLINK_DEBUG_FRAMEWORK_SPOT_DISCOVERY=1 \
    dotnet "$SERVER_DLL" \
    --role "$role" --http-url "http://127.0.0.1:0" "${metrics_args[@]}" \
    >"$LOG_DIR/${role}.stdout.log" 2>"$LOG_DIR/${role}.stderr.log" &
  PIDS+=("$!")
  ROLE_PIDS+=("$!")
  ROLE_PIDS_BY_NAME["$role"]="$!"
}

wait_role() {
  local role="$1"
  ROLE_URL=""
  for _ in $(seq 1 "$LOCAL_READINESS_ATTEMPTS"); do
    ROLE_URL="$(sed -n "s/.*observability-ops ready role=${role} url=//p" "$LOG_DIR/${role}.stdout.log" | tail -1)"
    if [[ -n "$ROLE_URL" ]]; then break; fi
    sleep "$LOCAL_READINESS_POLL_SECONDS"
  done
  if [[ -z "$ROLE_URL" ]]; then
    echo "Timed out waiting ${LOCAL_READINESS_TIMEOUT_SECONDS}s for ${role} evidence address" >&2
    return 1
  fi
  wait_health "$ROLE_URL"
}

start_primary_roles() {
  local role
  for role in play-a play-b session-a session-b api-a api-b workflow-a workflow-b; do
    launch_role "$role"
  done
  wait_role api-a; API_A_URL="$ROLE_URL"
  wait_role api-b; API_B_URL="$ROLE_URL"
  wait_role play-a; PLAY_A_URL="$ROLE_URL"
  wait_role play-b; PLAY_B_URL="$ROLE_URL"
  wait_role session-a; SESSION_A_URL="$ROLE_URL"
  wait_role session-b; SESSION_B_URL="$ROLE_URL"
  wait_role workflow-a; WORKFLOW_A_EVIDENCE_URL="$ROLE_URL"
  wait_role workflow-b; WORKFLOW_B_EVIDENCE_URL="$ROLE_URL"
}

start_bingo_roles() {
  local role
  for role in play-a play-b session-a session-b api-a api-b; do
    launch_role "$role"
  done
  wait_role api-a; API_A_URL="$ROLE_URL"
  wait_role api-b; API_B_URL="$ROLE_URL"
  wait_role play-a; PLAY_A_URL="$ROLE_URL"
  wait_role play-b; PLAY_B_URL="$ROLE_URL"
  wait_role session-a; SESSION_A_URL="$ROLE_URL"
  wait_role session-b; SESSION_B_URL="$ROLE_URL"
}

start_workflow_roles() {
  launch_role workflow-a
  launch_role workflow-b
  wait_role workflow-a; WORKFLOW_A_EVIDENCE_URL="$ROLE_URL"
  wait_role workflow-b; WORKFLOW_B_EVIDENCE_URL="$ROLE_URL"
}

if [[ "$MODE" == "drain" ]]; then
  start_primary_roles
else
  start_bingo_roles
fi

if [[ "$MODE" == "all" || "$MODE" == "flow" ]]; then
sleep 5
curl -fsS "$SESSION_A_URL/evidence" >"$LOG_DIR/session-a.b1-baseline.evidence.json"
curl -fsS "$PLAY_A_URL/evidence" >"$LOG_DIR/play-a.b2-baseline.evidence.json"
B1_RELEASE_FILE="$LOG_DIR/b1.release"
dotnet "$TRIGGER_DLL" \
  metrics-session "$BINGO_SESSION_A_STREAM_ENDPOINT" "$B1_RELEASE_FILE" \
  >"$LOG_DIR/b1-connections.stdout.log" 2>"$LOG_DIR/b1-connections.stderr.log" &
b1_connections_pid=$!
PIDS+=("$b1_connections_pid")
for _ in $(seq 1 200); do
  if grep -q "OBS-B1 connections=ready count=3" "$LOG_DIR/b1-connections.stdout.log"; then break; fi
  sleep 0.1
done
grep -q "OBS-B1 connections=ready count=3" "$LOG_DIR/b1-connections.stdout.log"
for _ in $(seq 1 100); do
  curl -fsS "$SESSION_A_URL/evidence" >"$LOG_DIR/session-a.b1-active.evidence.json"
  if python3 - "$LOG_DIR/session-a.b1-active.evidence.json" <<'PY'
import json
import sys
samples = json.load(open(sys.argv[1], encoding="utf-8"))["metrics"]
active = sum(sample["value"] for sample in samples
             if sample["name"] == "zlink.stream.connections.active")
raise SystemExit(0 if active == 3 else 1)
PY
  then
    break
  fi
  sleep 0.05
done
touch "$B1_RELEASE_FILE"
wait "$b1_connections_pid"
grep -q "OBS-B1 connections=released count=3" "$LOG_DIR/b1-connections.stdout.log"
for _ in $(seq 1 100); do
  curl -fsS "$SESSION_A_URL/evidence" >"$LOG_DIR/session-a.b1-after.evidence.json"
  if python3 - "$LOG_DIR/session-a.b1-after.evidence.json" <<'PY'
import json
import sys
samples = json.load(open(sys.argv[1], encoding="utf-8"))["metrics"]
active = sum(sample["value"] for sample in samples
             if sample["name"] == "zlink.stream.connections.active")
closed = sum(sample["value"] for sample in samples
             if sample["name"] == "zlink.stream.connections.closed")
raise SystemExit(0 if active == 0 and closed >= 3 else 1)
PY
  then
    break
  fi
  sleep 0.05
done
python3 - "$LOG_DIR/session-a.b1-baseline.evidence.json" "$LOG_DIR/session-a.b1-active.evidence.json" "$LOG_DIR/session-a.b1-after.evidence.json" <<'PY'
import json
import sys

def samples(path):
    return json.load(open(path, encoding="utf-8"))["metrics"]

def total(items, name):
    return sum(sample["value"] for sample in items if sample["name"] == name)

baseline, active, after = map(samples, sys.argv[1:])
if total(baseline, "zlink.stream.connections.active") != 0:
    raise SystemExit("OBS-B1: baseline active connection gauge was not zero")
if total(active, "zlink.stream.connections.active") != 3:
    raise SystemExit("OBS-B1: active connection gauge did not reach three")
if total(after, "zlink.stream.connections.active") != 0:
    raise SystemExit("OBS-B1: active connection gauge did not return to zero")
if total(after, "zlink.stream.connections.opened") - total(baseline, "zlink.stream.connections.opened") != 3:
    raise SystemExit("OBS-B1: opened counter delta did not equal three")
if total(after, "zlink.stream.connections.closed") - total(baseline, "zlink.stream.connections.closed") != 3:
    raise SystemExit("OBS-B1: closed counter delta did not equal three")
allowed = {"client_close", "idle_timeout", "heartbeat_timeout", "server_drain", "protocol_error", "transport_error"}
for sample in after:
    if sample["name"] == "zlink.stream.connections.closed" and sample["tags"].get("close_reason") not in allowed:
        raise SystemExit("OBS-B1: close_reason is outside the closed enum")
print("OBS-B1 connection gauges PASS")
PY
dotnet "$TRIGGER_DLL" \
  metrics-reconnect "$BINGO_SESSION_A_STREAM_ENDPOINT" \
  >"$LOG_DIR/b1-reconnect.stdout.log" 2>"$LOG_DIR/b1-reconnect.stderr.log" &
b1_reconnect_pid=$!
PIDS+=("$b1_reconnect_pid")
for _ in $(seq 1 200); do
  if grep -q "OBS-B1 reconnect=ready" "$LOG_DIR/b1-reconnect.stdout.log"; then break; fi
  sleep 0.1
done
grep -q "OBS-B1 reconnect=ready" "$LOG_DIR/b1-reconnect.stdout.log"
session_a_pid="${ROLE_PIDS_BY_NAME[session-a]}"
kill -KILL "$session_a_pid"
wait "$session_a_pid" 2>/dev/null || true
launch_role session-a
wait_role session-a; SESSION_A_URL="$ROLE_URL"
wait "$b1_reconnect_pid"
grep -Eq "OBS-B1 reconnect=completed attempts=[1-9][0-9]*" "$LOG_DIR/b1-reconnect.stdout.log"
echo "OBS-B1 reconnect metric PASS"
dotnet "$CLIENT_DLL" \
  --stream-a-endpoint "$BINGO_SESSION_A_STREAM_ENDPOINT" \
  --stream-b-endpoint "$BINGO_SESSION_B_STREAM_ENDPOINT" \
  >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"
dotnet "$TRIGGER_DLL" \
  error "$BINGO_SESSION_A_STREAM_ENDPOINT" \
  >"$LOG_DIR/trigger.stdout.log" 2>"$LOG_DIR/trigger.stderr.log"
rg -o --no-filename 'flow=[0-9a-f-]{36}' "$LOG_DIR"/flow-*.log \
  | cut -d= -f2 | sort -u >"$LOG_DIR/pre-off-flow-ids.txt"
curl -fsS -X POST "$API_A_URL/message-flow/off" >"$LOG_DIR/api-a.flow-off.json"
curl -fsS -X POST "$API_B_URL/message-flow/off" >"$LOG_DIR/api-b.flow-off.json"
for _ in $(seq 1 200); do
  curl -fsS "$PLAY_A_URL/evidence" >"$LOG_DIR/play-a.pre-off-action.evidence.json"
  curl -fsS "$PLAY_B_URL/evidence" >"$LOG_DIR/play-b.pre-off-action.evidence.json"
  if python3 - "$LOG_DIR/play-a.pre-off-action.evidence.json" "$LOG_DIR/play-b.pre-off-action.evidence.json" <<'PY'
import json
import sys
rows = []
for path in sys.argv[1:]:
    rows.extend(json.load(open(path, encoding="utf-8"))["actorRows"])
raise SystemExit(1 if any(row["actorId"] == "player-1" for row in rows) else 0)
PY
  then
    break
  fi
  sleep 0.1
done
python3 - "$LOG_DIR/play-a.pre-off-action.evidence.json" "$LOG_DIR/play-b.pre-off-action.evidence.json" <<'PY'
import json
import sys
rows = []
for path in sys.argv[1:]:
    rows.extend(json.load(open(path, encoding="utf-8"))["actorRows"])
if any(row["actorId"] == "player-1" for row in rows):
    raise SystemExit("OBS-A3: prior player-1 actor did not clean up before the off-node action")
PY
dotnet "$TRIGGER_DLL" \
  flow-action "$BINGO_SESSION_A_STREAM_ENDPOINT" \
  >"$LOG_DIR/off-flow-action.stdout.log" 2>"$LOG_DIR/off-flow-action.stderr.log"
curl -fsS "$SESSION_A_URL/evidence" >"$LOG_DIR/session-a.evidence.json"
curl -fsS "$PLAY_A_URL/evidence" >"$LOG_DIR/play-a.evidence.json"
curl -fsS "$PLAY_B_URL/evidence" >"$LOG_DIR/play-b.no-reader.evidence.json"
python3 - "$LOG_DIR/play-a.b2-baseline.evidence.json" "$LOG_DIR/play-a.evidence.json" <<'PY'
import json
import sys

baseline = json.load(open(sys.argv[1], encoding="utf-8"))["metrics"]
after = json.load(open(sys.argv[2], encoding="utf-8"))["metrics"]

def one(items, name, **tags):
    matches = [sample for sample in items
               if sample["name"] == name
               and all(sample["tags"].get(key) == value for key, value in tags.items())]
    return matches[0] if matches else None

user_depth = one(after, "zlink.spot.queue.depth", kind="user")
entry_depth = one(after, "zlink.spot.queue.depth", kind="entry")
if user_depth is None or entry_depth is None:
    raise SystemExit("OBS-B2: entry/user SPOT queue series are not separated")
if user_depth["value"] != 0 or entry_depth["value"] != 0:
    raise SystemExit("OBS-B2: SPOT queue depth did not return to zero")
before_wait = one(baseline, "zlink.spot.queue.wait.duration")
after_wait = one(after, "zlink.spot.queue.wait.duration")
if after_wait is None or after_wait["count"] <= (before_wait["count"] if before_wait else 0):
    raise SystemExit("OBS-B2: SPOT queue wait histogram did not record workload")
if after_wait["min"] is None or after_wait["max"] is None or after_wait["sum"] is None:
    raise SystemExit("OBS-B2: SPOT queue wait histogram summary is incomplete")
print("OBS-B2 queue metrics PASS")
PY
python3 - "$LOG_DIR/play-b.no-reader.evidence.json" <<'PY'
import json
import sys
evidence = json.load(open(sys.argv[1], encoding="utf-8"))
if evidence["metrics"] or evidence["metricSeriesStored"] != 0:
    raise SystemExit("OBS-B4: reader-off node retained metric samples")
print("OBS-B4 PASS")
PY
stop_processes "${ROLE_PIDS[@]}"
ROLE_PIDS=()
start_workflow_roles
curl -fsS "$WORKFLOW_A_EVIDENCE_URL/evidence" >"$LOG_DIR/workflow-a.b3-baseline.evidence.json"
curl -fsS "$WORKFLOW_B_EVIDENCE_URL/evidence" >"$LOG_DIR/workflow-b.b3-baseline.evidence.json"
FANOUT_B_REQUEST='{"orderId":"observability-fanout-b","cartId":"cart-success","shippingAddressId":"addr-home","paymentMethodId":"pm-ok","idempotencyKey":"observability-fanout-b-key","lines":[{"sku":"sku-mouse","quantity":1}],"amount":40.00,"currency":"USD"}'
FANOUT_A_REQUEST='{"orderId":"observability-fanout-a","cartId":"cart-success","shippingAddressId":"addr-home","paymentMethodId":"pm-ok","idempotencyKey":"observability-fanout-a-key","lines":[{"sku":"sku-keyboard","quantity":1}],"amount":80.00,"currency":"USD"}'
curl -fsS -H 'content-type: application/json' --data "$FANOUT_B_REQUEST" \
  "$SHOPPINGMALL_WORKFLOW_B_HTTP_URL/self-check/workflow/inventory-reserved" \
  >"$LOG_DIR/workflow-b.fanout-seed.json"
curl -fsS "$WORKFLOW_A_EVIDENCE_URL/evidence" >"$LOG_DIR/workflow-a.b3-pre-fanout.evidence.json"
curl -fsS "$WORKFLOW_B_EVIDENCE_URL/evidence" >"$LOG_DIR/workflow-b.b3-pre-fanout.evidence.json"
curl -fsS -H 'content-type: application/json' --data "$FANOUT_A_REQUEST" \
  "$SHOPPINGMALL_WORKFLOW_A_HTTP_URL/self-check/workflow/inventory-reserved" \
  >"$LOG_DIR/workflow-a.fanout-publish.json"
for _ in $(seq 1 100); do
  if grep -q "order=observability-fanout-a" "$LOG_DIR/workflow-a.stdout.log" \
    && grep -q "order=observability-fanout-a" "$LOG_DIR/workflow-b.stdout.log"; then
    break
  fi
  sleep 0.1
done
curl -fsS "$WORKFLOW_A_EVIDENCE_URL/evidence" >"$LOG_DIR/workflow-a.b3-fanout.evidence.json"
curl -fsS "$WORKFLOW_B_EVIDENCE_URL/evidence" >"$LOG_DIR/workflow-b.b3-fanout.evidence.json"
docker exec "$REDIS_CONTAINER" redis-cli CLIENT PAUSE 11000 ALL \
  >"$LOG_DIR/b3-redis-pause.log"
for _ in $(seq 1 100); do
  if curl -fsS --max-time 2 "$WORKFLOW_A_EVIDENCE_URL/evidence" \
      >"$LOG_DIR/workflow-a.b3-delayed-lease.evidence.json" 2>/dev/null \
    && python3 - "$LOG_DIR/workflow-a.b3-delayed-lease.evidence.json" <<'PY'
import json
import sys
samples = json.load(open(sys.argv[1], encoding="utf-8"))["metrics"]
late = [sample for sample in samples
        if sample["name"] == "zlink.location.owner_lease.renew.lateness"]
raise SystemExit(0 if late and max(sample.get("max") or 0 for sample in late) >= 0.5 else 1)
PY
  then
    break
  fi
  sleep 0.2
done
python3 - "$LOG_DIR/workflow-a.b3-delayed-lease.evidence.json" <<'PY'
import json
import sys
samples = json.load(open(sys.argv[1], encoding="utf-8"))["metrics"]
late = [sample for sample in samples
        if sample["name"] == "zlink.location.owner_lease.renew.lateness"]
if not late or max(sample.get("max") or 0 for sample in late) < 0.5:
    raise SystemExit("OBS-B3: external Redis pause did not produce lease renewal lateness")
PY

grep -q "bingo=completed" "$LOG_DIR/client.log"
grep -q "OBS-A2 trigger=completed" "$LOG_DIR/trigger.stdout.log"
grep -q "OBS-A3 action=completed" "$LOG_DIR/off-flow-action.stdout.log"
grep -Rq "message flow" "$LOG_DIR"/flow-*.log
grep -Eq '"name":"zlink\.stream\.connections\.(active|opened)"' "$LOG_DIR/session-a.evidence.json"
grep -Eq '"name":"zlink\.spot\.(count|queue\.depth)"' "$LOG_DIR/play-a.evidence.json"

python3 - "$LOG_DIR" <<'PY'
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
pre_off_flows = set((root / "pre-off-flow-ids.txt").read_text(encoding="utf-8").splitlines())
events = []
for path in root.glob("flow-*.log"):
    for line in path.read_text(encoding="utf-8").splitlines():
        fields = {}
        for token in line.split():
            if "=" in token:
                key, value = token.split("=", 1)
                fields[key] = value
        if "flow" in fields:
            events.append((line[:33], path.name, fields, line))

session_matches = [event for event in events
                   if event[2].get("label") == "session-1101"
                   and event[2].get("surface") == "StreamSession"
                   and event[2].get("packet") == "MatchBingoReq"
                   and event[2].get("phase") == "received"
                   and event[2].get("origin") == "application"]
crossing = None
for candidate in session_matches:
    flow = candidate[2]["flow"]
    same = [event for event in events if event[2].get("flow") == flow]
    has_api = any(event[2].get("label", "").startswith("api-")
                  and event[2].get("packet") == "MatchBingoApiReq" for event in same)
    has_play = any(event[2].get("label", "").startswith("play-")
                   and event[2].get("packet") == "AllocateBingoRoomReq" for event in same)
    has_spot = any(event[2].get("label", "").startswith("play-")
                   and event[2].get("surface") in {"SpotRoute", "SpotActor"} for event in same)
    if has_api and has_play and has_spot:
        crossing = sorted(same)
        break
if crossing is None:
    raise SystemExit("OBS-A1: dedicated MatchBingo flow did not cross Session, actor/API, Play, and room Spot")
if [event[0] for event in crossing] != sorted(event[0] for event in crossing):
    raise SystemExit("OBS-A1: flow evidence is not in timestamp order")

missing = [event for event in events
           if event[2].get("packet") == "ObservabilityMissingPacket"]
missing_flows = {event[2]["flow"] for event in missing
                 if event[2].get("label") == "session-1101"
                 and event[2].get("phase") == "received"}
if not any(event[2].get("phase") == "error"
           and event[2].get("flow") in missing_flows for event in missing):
    raise SystemExit("OBS-A2: missing-packet received and error lines do not share one flow")

off_candidates = [event for event in events
                  if event[2].get("label") == "session-1101"
                  and event[2].get("surface") == "StreamSession"
                  and event[2].get("packet") == "MatchBingoReq"
                  and event[2].get("phase") == "received"
                  and event[2].get("flow") not in pre_off_flows]
if len({event[2]["flow"] for event in off_candidates}) != 1:
    raise SystemExit("OBS-A3: off-node action did not create exactly one new connector flow")
off_flow = off_candidates[0][2]["flow"]
off_events = [event for event in events if event[2].get("flow") == off_flow]
if not any(event[2].get("label", "").startswith("play-")
           and event[2].get("packet") == "AllocateBingoRoomReq" for event in off_events):
    raise SystemExit("OBS-A3: flow did not propagate through the off API node to Play")
if any(event[2].get("label", "").startswith("api-") for event in off_events):
    raise SystemExit("OBS-A3: tracing-off API node emitted a flow event")

fanout_events = [event for event in events
                 if event[2].get("surface") == "SpotSubscription"
                 and event[2].get("packet") == "OrderProjectionUpdatedEvent"
                 and event[2].get("phase") == "received"]
fanout_by_flow = {}
for event in fanout_events:
    fanout_by_flow.setdefault(event[2]["flow"], set()).add(event[2].get("label"))
if not any({"workflow-a", "workflow-b"}.issubset(labels)
           for labels in fanout_by_flow.values()):
    raise SystemExit("OBS-A4: OrderWorkflow projection did not fan out one flow to both nodes")
if not any(event[2].get("origin") == "timer"
           and event[2].get("surface") == "SpotSubscription" for event in events):
    raise SystemExit("OBS-A4: room timer did not start a timer-origin flow")
print("OBS-A1 PASS")
print("OBS-A2 PASS")
print("OBS-A3 PASS")
print("OBS-A4 PASS")

def metric_total(paths, name):
    total = 0
    for path in paths:
        evidence = json.load(open(path, encoding="utf-8"))
        total += sum(sample["value"] for sample in evidence["metrics"] if sample["name"] == name)
    return total

baseline_paths = [root / "workflow-a.b3-pre-fanout.evidence.json", root / "workflow-b.b3-pre-fanout.evidence.json"]
fanout_paths = [root / "workflow-a.b3-fanout.evidence.json", root / "workflow-b.b3-fanout.evidence.json"]
published_delta = metric_total(fanout_paths, "zlink.fanout.published") - metric_total(baseline_paths, "zlink.fanout.published")
received_delta = metric_total(fanout_paths, "zlink.fanout.received") - metric_total(baseline_paths, "zlink.fanout.received")
if published_delta != 1 or received_delta != 2:
    raise SystemExit(f"OBS-B3: expected fanout 1:2, got {published_delta}:{received_delta}")
for path in fanout_paths:
    samples = json.load(open(path, encoding="utf-8"))["metrics"]
    if any(sample["name"] == "zlink.fanout.dropped" for sample in samples):
        raise SystemExit("OBS-B3: unobservable fanout backend exposed fanout.dropped")
    for sample in samples:
        forbidden = {"correlation_id", "flow_id", "actor_id", "spot_rid"} & set(sample["tags"])
        if forbidden:
            raise SystemExit(f"OBS-B3: forbidden high-cardinality metric labels: {sorted(forbidden)}")
print("OBS-B3 PASS")
print("OBS-B1/B2 evidence PASS")
PY

if [[ "$MODE" == "flow" ]]; then
  echo "ObservabilityOps flow fixture PASS log_dir=$LOG_DIR"
  exit 0
fi

stop_processes "${ROLE_PIDS[@]}"
ROLE_PIDS=()
export BINGO_REDIS_KEY_PREFIX="observability-ops:${RUN_ID}:drain:"
export SHOPPINGMALL_REDIS_KEY_PREFIX="observability-ops:${RUN_ID}:shoppingmall:drain:"
start_primary_roles
sleep 5
else
sleep 5
fi

dotnet "$TRIGGER_DLL" \
  hold-play "$BINGO_SESSION_A_STREAM_ENDPOINT" "$BINGO_SESSION_B_STREAM_ENDPOINT" \
  >"$LOG_DIR/hold-play.stdout.log" 2>"$LOG_DIR/hold-play.stderr.log" &
hold_play_pid=$!
PIDS+=("$hold_play_pid")
for _ in $(seq 1 200); do
  if grep -q "OBS-C1 hold=ready" "$LOG_DIR/hold-play.stdout.log"; then break; fi
  sleep 0.1
done
grep -q "OBS-C1 hold=ready" "$LOG_DIR/hold-play.stdout.log"
curl -fsS "$PLAY_A_URL/evidence" >"$LOG_DIR/play-a.before-drain.evidence.json"
curl -fsS "$PLAY_A_URL/drain?deadlineMs=30000" >"$LOG_DIR/play-a.drain.json" &
play_drain_pid=$!
PIDS+=("$play_drain_pid")
sleep 1
curl -fsS "$PLAY_A_URL/evidence" >"$LOG_DIR/play-a.draining.evidence.json"
python3 - "$LOG_DIR/play-a.before-drain.evidence.json" "$LOG_DIR/play-a.draining.evidence.json" <<'PY'
import json
import sys
before = json.load(open(sys.argv[1], encoding="utf-8"))
evidence = json.load(open(sys.argv[2], encoding="utf-8"))
if evidence["ready"]:
    raise SystemExit("OBS-C1: drain did not flip readiness")
if not any(row["draining"] for row in evidence["peerRows"]):
    raise SystemExit("OBS-C1: typed draining peer row missing")
before_rooms = {row["spotRid"] for row in before["spotRows"] if row["kind"] == "User"}
draining_rooms = {row["spotRid"] for row in evidence["spotRows"] if row["kind"] == "User"}
if not before_rooms or not before_rooms.issubset(draining_rooms):
    raise SystemExit("OBS-C1/C3: drain-natural removed an active room before natural release")
print("OBS-C1 PASS")
PY
for _ in $(seq 1 34); do
  sleep 0.5
  curl -fsS "$PLAY_A_URL/evidence" >"$LOG_DIR/play-a.handoff.evidence.json"
  if python3 - "$LOG_DIR/play-a.before-drain.evidence.json" "$LOG_DIR/play-a.handoff.evidence.json" <<'PY'
import json
import sys
before = json.load(open(sys.argv[1], encoding="utf-8"))
after = json.load(open(sys.argv[2], encoding="utf-8"))
before_actors = {row["actorId"]: row["nodeRid"] for row in before["actorRows"]}
after_actors = {row["actorId"]: row["nodeRid"] for row in after["actorRows"]}
if not any(actor in after_actors and after_actors[actor] != node
           for actor, node in before_actors.items()):
    raise SystemExit(1)
if not any(sample["name"] == "zlink.drain.actors.handed_off"
           for sample in after["metrics"]):
    raise SystemExit(1)
PY
  then
    echo "OBS-C2 PASS"
    break
  fi
done
python3 - "$LOG_DIR/play-a.before-drain.evidence.json" "$LOG_DIR/play-a.handoff.evidence.json" <<'PY'
import json
import sys
before = json.load(open(sys.argv[1], encoding="utf-8"))
after = json.load(open(sys.argv[2], encoding="utf-8"))
before_actors = {row["actorId"]: row["nodeRid"] for row in before["actorRows"]}
after_actors = {row["actorId"]: row["nodeRid"] for row in after["actorRows"]}
moved = sum(actor in after_actors and after_actors[actor] != node
            for actor, node in before_actors.items())
if moved == 0:
    raise SystemExit("OBS-C2: actor location did not move to another node")

def value(evidence, name):
    return sum(sample["value"] for sample in evidence["metrics"] if sample["name"] == name)

def count(evidence, name):
    return sum(sample["count"] for sample in evidence["metrics"] if sample["name"] == name)

if value(after, "zlink.drain.actors.handed_off") - value(before, "zlink.drain.actors.handed_off") != moved:
    raise SystemExit("OBS-C2/B2: actor handoff counter does not match moved actors")
if value(after, "zlink.actor.transfers") - value(before, "zlink.actor.transfers") != moved:
    raise SystemExit("OBS-B2: actor transfer counter does not match completed moves")
if count(after, "zlink.actor.transfer.duration") - count(before, "zlink.actor.transfer.duration") != moved:
    raise SystemExit("OBS-B2: actor transfer duration sample count is wrong")
if count(after, "zlink.actor.transfer.pending_requests.count") - count(before, "zlink.actor.transfer.pending_requests.count") != moved:
    raise SystemExit("OBS-B2: pending-request sample was not recorded once per move")
print("OBS-B2 actor transfer metrics PASS")
PY
wait "$hold_play_pid"
wait "$play_drain_pid"
grep -q '"result":"drained"' "$LOG_DIR/play-a.drain.json"
grep -q "OBS-C2 bound_push=continued target=2202" "$LOG_DIR/hold-play.stdout.log"
grep -q "OBS-C3 hold=released" "$LOG_DIR/hold-play.stdout.log"
curl -fsS "$PLAY_A_URL/evidence" >"$LOG_DIR/play-a.drained.evidence.json"
python3 - "$LOG_DIR/play-a.before-drain.evidence.json" "$LOG_DIR/play-a.drained.evidence.json" <<'PY'
import json
import sys
before = json.load(open(sys.argv[1], encoding="utf-8"))
after = json.load(open(sys.argv[2], encoding="utf-8"))
before_rooms = {row["spotRid"] for row in before["spotRows"] if row["kind"] == "User"}
after_rooms = {row["spotRid"] for row in after["spotRows"] if row["kind"] == "User"}
if before_rooms & after_rooms:
    raise SystemExit("OBS-C3: naturally released room remains registered")
if not any(sample["name"] == "zlink.drain.rooms.drained"
           and sample["tags"].get("policy") == "drain_natural"
           and sample["value"] >= 1
           for sample in after["metrics"]):
    raise SystemExit("OBS-C3: drain-natural room metric is missing")
PY
echo "OBS-C3 drain-natural PASS"

ORDER_ID="observability-release-recreate"
ORDER_REQUEST='{"orderId":"observability-release-recreate","cartId":"cart-success","shippingAddressId":"addr-home","paymentMethodId":"pm-ok","idempotencyKey":"observability-release-recreate-key","lines":[{"sku":"sku-keyboard","quantity":1}],"amount":120.00,"currency":"USD"}'
curl -fsS -H 'content-type: application/json' \
  --data "$ORDER_REQUEST" \
  "$SHOPPINGMALL_WORKFLOW_A_HTTP_URL/self-check/workflow/inventory-reserved" \
  >"$LOG_DIR/workflow-a.checkpoint.json"
curl -fsS "$WORKFLOW_A_EVIDENCE_URL/evidence" >"$LOG_DIR/workflow-a.before-drain.evidence.json"
python3 - "$LOG_DIR/workflow-a.checkpoint.json" "$LOG_DIR/workflow-a.before-drain.evidence.json" "$ORDER_ID" <<'PY'
import json
import sys
checkpoint = json.load(open(sys.argv[1], encoding="utf-8"))
evidence = json.load(open(sys.argv[2], encoding="utf-8"))
order_id = sys.argv[3]
if checkpoint["state"]["status"] != "InventoryReserved":
    raise SystemExit("OBS-C3: workflow checkpoint was not persisted")
if not any(row["spotRid"] == order_id and row["nodeRid"] == "6101"
           for row in evidence["spotRows"]):
    raise SystemExit("OBS-C3: workflow-a did not own the event-sourcing spot")
PY
curl -fsS -X POST \
  "$SHOPPINGMALL_WORKFLOW_A_HTTP_URL/self-check/projection/$ORDER_ID/delete" \
  >"$LOG_DIR/workflow-a.projection-delete.json"
curl -fsS "$WORKFLOW_A_EVIDENCE_URL/drain?deadlineMs=30000" \
  >"$LOG_DIR/workflow-a.drain.json"
grep -q '"result":"drained"' "$LOG_DIR/workflow-a.drain.json"
for _ in $(seq 1 100); do
  curl -fsS "$WORKFLOW_B_EVIDENCE_URL/evidence" >"$LOG_DIR/workflow.after-release.evidence.json"
  if python3 - "$LOG_DIR/workflow.after-release.evidence.json" "$ORDER_ID" <<'PY'
import json
import sys
evidence = json.load(open(sys.argv[1], encoding="utf-8"))
order_id = sys.argv[2]
raise SystemExit(1 if any(row["spotRid"] == order_id for row in evidence["spotRows"]) else 0)
PY
  then
    break
  fi
  sleep 0.1
done
python3 - "$LOG_DIR/workflow.after-release.evidence.json" "$ORDER_ID" <<'PY'
import json
import sys
evidence = json.load(open(sys.argv[1], encoding="utf-8"))
order_id = sys.argv[2]
if any(row["spotRid"] == order_id for row in evidence["spotRows"]):
    raise SystemExit("OBS-C3: release-and-recreate left the owner row registered")
PY
curl -fsS -X POST \
  "$SHOPPINGMALL_WORKFLOW_B_HTTP_URL/self-check/projection/$ORDER_ID/rebuild" \
  >"$LOG_DIR/workflow-b.rebuild.json"
curl -fsS "$WORKFLOW_B_EVIDENCE_URL/evidence" >"$LOG_DIR/workflow-b.recreated.evidence.json"
curl -fsS "$WORKFLOW_A_EVIDENCE_URL/evidence" >"$LOG_DIR/workflow-a.drained.evidence.json"
python3 - "$LOG_DIR/workflow-b.rebuild.json" "$LOG_DIR/workflow-b.recreated.evidence.json" "$LOG_DIR/workflow-a.drained.evidence.json" "$ORDER_ID" <<'PY'
import json
import sys
rebuild = json.load(open(sys.argv[1], encoding="utf-8"))
recreated = json.load(open(sys.argv[2], encoding="utf-8"))
drained = json.load(open(sys.argv[3], encoding="utf-8"))
order_id = sys.argv[4]
if rebuild["state"]["status"] != "InventoryReserved":
    raise SystemExit("OBS-C3: event replay did not restore the deleted projection")
if not any(row["spotRid"] == order_id and row["nodeRid"] == "6102"
           for row in recreated["spotRows"]):
    raise SystemExit("OBS-C3: next request did not recreate the spot on workflow-b")
if not any(sample["name"] == "zlink.drain.rooms.drained"
           and sample["tags"].get("policy") == "release_and_recreate"
           and sample["value"] >= 1
           for sample in drained["metrics"]):
    raise SystemExit("OBS-C3: release-and-recreate drain metric is missing")
print("OBS-C3 release-and-recreate PASS")
PY

dotnet "$TRIGGER_DLL" \
  idle-session "$BINGO_SESSION_B_STREAM_ENDPOINT" \
  >"$LOG_DIR/idle-session.stdout.log" 2>"$LOG_DIR/idle-session.stderr.log"
grep -q "OBS-C4 idle_close_reason=IdleTimeout" "$LOG_DIR/idle-session.stdout.log"

dotnet "$TRIGGER_DLL" \
  heartbeat-session "$BINGO_SESSION_B_STREAM_ENDPOINT" \
  >"$LOG_DIR/heartbeat-session.stdout.log" 2>"$LOG_DIR/heartbeat-session.stderr.log"
grep -q "OBS-C4 heartbeat_close_reason=HeartbeatTimeout" "$LOG_DIR/heartbeat-session.stdout.log"
echo "OBS-C4 liveness PASS"

dotnet "$TRIGGER_DLL" \
  hold-session "$BINGO_SESSION_B_STREAM_ENDPOINT" \
  >"$LOG_DIR/hold-session.stdout.log" 2>"$LOG_DIR/hold-session.stderr.log" &
hold_session_pid=$!
PIDS+=("$hold_session_pid")
for _ in $(seq 1 100); do
  if grep -q "OBS-C4 session=ready" "$LOG_DIR/hold-session.stdout.log"; then break; fi
  sleep 0.1
done
grep -q "OBS-C4 session=ready" "$LOG_DIR/hold-session.stdout.log"
curl -fsS "$SESSION_B_URL/drain?deadlineMs=1" >"$LOG_DIR/session-b.force-drain.json"
wait "$hold_session_pid"
grep -q '"result":"force_stopped"' "$LOG_DIR/session-b.force-drain.json"
grep -q "OBS-C4 close_reason=ServerDrain" "$LOG_DIR/hold-session.stdout.log"
echo "OBS-C4 PASS"

stop_processes "${ROLE_PIDS[@]}"
ROLE_PIDS=()
export BINGO_REDIS_KEY_PREFIX="observability-ops:${RUN_ID}:c5:"
start_bingo_roles
sleep 5

dotnet "$TRIGGER_DLL" \
  hold-play "$BINGO_SESSION_A_STREAM_ENDPOINT" "$BINGO_SESSION_B_STREAM_ENDPOINT" \
  >"$LOG_DIR/c5-hold-play.stdout.log" 2>"$LOG_DIR/c5-hold-play.stderr.log" &
c5_hold_pid=$!
PIDS+=("$c5_hold_pid")
for _ in $(seq 1 200); do
  if grep -q "OBS-C1 hold=ready" "$LOG_DIR/c5-hold-play.stdout.log"; then break; fi
  sleep 0.1
done
grep -q "OBS-C1 hold=ready" "$LOG_DIR/c5-hold-play.stdout.log"
curl -fsS "$PLAY_A_URL/evidence" >"$LOG_DIR/c5-play-a.before-drain.evidence.json"
curl -fsS "$PLAY_A_URL/drain?deadlineMs=12000" >"$LOG_DIR/c5-play-a.drain.json" &
c5_play_a_drain_pid=$!
PIDS+=("$c5_play_a_drain_pid")
curl -fsS "$PLAY_B_URL/drain?deadlineMs=12000" >"$LOG_DIR/c5-play-b.drain.json" &
c5_play_b_drain_pid=$!
PIDS+=("$c5_play_b_drain_pid")
sleep 7
curl -fsS "$PLAY_A_URL/evidence" >"$LOG_DIR/c5-play-a.zero-target.evidence.json"
python3 - "$LOG_DIR/c5-play-a.zero-target.evidence.json" <<'PY'
import json
import sys
evidence = json.load(open(sys.argv[1], encoding="utf-8"))
actors = {row["actorId"]: row["nodeRid"] for row in evidence["actorRows"]}
if actors.get("player-1") != "2201" or actors.get("player-2") != "2201":
    raise SystemExit("OBS-C5: actors did not remain on the source while no target was serving")
if any(sample["name"] == "zlink.drain.actors.handed_off" for sample in evidence["metrics"]):
    raise SystemExit("OBS-C5: zero-target drain unexpectedly handed off an actor")
if evidence["ready"]:
    raise SystemExit("OBS-C5: source remained ready during simultaneous drain")
print("OBS-C5 zero-target=held-until-deadline")
PY
wait "$c5_play_a_drain_pid"
wait "$c5_play_b_drain_pid"
grep -q '"result":"force_stopped","reason":"DeadlineExceeded"' "$LOG_DIR/c5-play-a.drain.json"
grep -Eq '"result":"(drained|force_stopped)"' "$LOG_DIR/c5-play-b.drain.json"
stop_processes "$c5_hold_pid"
echo "OBS-C5 PASS"

echo "ObservabilityOps drain fixture PASS log_dir=$LOG_DIR"
