#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODE="${1:-all}"
source "$ROOT_DIR/../redis-common.sh"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"
export BINGO_LOG_DIR="$LOG_DIR"
export BINGO_REDIS_KEY_PREFIX="observability-ops:${RUN_ID}:"

SERVER_PROJECT="$ROOT_DIR/Server/ObservabilityOps.Server.csproj"
TRIGGER_PROJECT="$ROOT_DIR/Trigger/ObservabilityOps.Trigger.csproj"
CLIENT_PROJECT="$ROOT_DIR/../../samples/Bingo/Client/Bingo.Client.csproj"
SERVER_DLL="$ROOT_DIR/Server/bin/Debug/net8.0/ObservabilityOps.Server.dll"
TRIGGER_DLL="$ROOT_DIR/Trigger/bin/Debug/net8.0/ObservabilityOps.Trigger.dll"
CLIENT_DLL="$ROOT_DIR/../../samples/Bingo/Client/bin/Debug/net8.0/Bingo.Client.dll"
PIDS=()
ROLE_PIDS=()

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
  if [[ -n "${REDIS_CONTAINER:-}" ]]; then docker rm -f "$REDIS_CONTAINER" >/dev/null 2>&1 || true; fi
  if [[ "$code" -ne 0 ]]; then echo "ObservabilityOps failed. Logs: $LOG_DIR" >&2; fi
}
trap cleanup EXIT

read -r -a PORTS <<<"$(python3 - <<'PY'
import socket
sockets = []
try:
    for _ in range(22):
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

zlink_redis_start_scoped_assign \
  REDIS_CONTAINER REDIS_ENDPOINT \
  "zlink-dotnet-observability-ops-${RUN_ID}" \
  "redis:7.2-alpine" "$LOG_DIR"
export BINGO_REDIS_ENDPOINT="$REDIS_ENDPOINT"

dotnet build "$SERVER_PROJECT" --no-restore --maxcpucount:1 >/dev/null
dotnet build "$TRIGGER_PROJECT" --no-restore --maxcpucount:1 >/dev/null
dotnet build "$CLIENT_PROJECT" --no-restore --maxcpucount:1 >/dev/null

wait_health() {
  local url="$1"
  for _ in $(seq 1 300); do
    if curl -fsS --max-time 1 "$url/health" >/dev/null 2>&1; then return 0; fi
    sleep 0.1
  done
  echo "Timed out waiting for $url" >&2
  return 1
}

start_role() {
  local role="$1"
  local http_port="$2"
  ROLE_URL="http://127.0.0.1:${http_port}"
  env ZLINK_DEBUG_FRAMEWORK_SPOT_DISCOVERY=1 \
    dotnet "$SERVER_DLL" \
    --role "$role" --http-url "$ROLE_URL" \
    >"$LOG_DIR/${role}.stdout.log" 2>"$LOG_DIR/${role}.stderr.log" &
  PIDS+=("$!")
  ROLE_PIDS+=("$!")
  wait_health "$ROLE_URL"
}

start_role api-a "${PORTS[14]}"; API_A_URL="$ROLE_URL"
start_role api-b "${PORTS[15]}"; API_B_URL="$ROLE_URL"
start_role play-a "${PORTS[16]}"; PLAY_A_URL="$ROLE_URL"
start_role play-b "${PORTS[17]}"; PLAY_B_URL="$ROLE_URL"
start_role session-a "${PORTS[18]}"; SESSION_A_URL="$ROLE_URL"
start_role session-b "${PORTS[19]}"; SESSION_B_URL="$ROLE_URL"

if [[ "$MODE" == "all" ]]; then
sleep 5
dotnet "$CLIENT_DLL" \
  --stream-a-endpoint "$BINGO_SESSION_A_STREAM_ENDPOINT" \
  --stream-b-endpoint "$BINGO_SESSION_B_STREAM_ENDPOINT" \
  >"$LOG_DIR/client.stdout.log" 2>"$LOG_DIR/client.stderr.log"
dotnet "$TRIGGER_DLL" \
  error "$BINGO_SESSION_A_STREAM_ENDPOINT" \
  >"$LOG_DIR/trigger.stdout.log" 2>"$LOG_DIR/trigger.stderr.log"

curl -fsS "$SESSION_A_URL/evidence" >"$LOG_DIR/session-a.evidence.json"
curl -fsS "$PLAY_A_URL/evidence" >"$LOG_DIR/play-a.evidence.json"
grep -q "bingo=completed" "$LOG_DIR/client.log"
grep -q "OBS-A2 trigger=completed" "$LOG_DIR/trigger.stdout.log"
grep -Rq "message flow" "$LOG_DIR"/flow-*.log
grep -Eq '"name":"zlink\.stream\.connections\.(active|opened)"' "$LOG_DIR/session-a.evidence.json"
grep -Eq '"name":"zlink\.spot\.(count|queue\.depth)"' "$LOG_DIR/play-a.evidence.json"

python3 - "$LOG_DIR" <<'PY'
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])
flow_sets = []
for name in ("flow-session.log", "flow-play.log"):
    text = (root / name).read_text(encoding="utf-8")
    flow_sets.append(set(re.findall(r"\bflow=([0-9a-f-]{36})", text)))
if not set.intersection(*flow_sets):
    raise SystemExit("OBS-A1: no flow id crossed Session and Play")
error_lines = [line for path in root.glob("flow-*.log") for line in path.read_text().splitlines()
               if "outcome=error" in line]
if not error_lines or not all(" flow=" in line for line in error_lines):
    raise SystemExit("OBS-A2: error flow evidence missing")
print("OBS-A1 PASS")
print("OBS-A2 PASS")
print("OBS-B1/B2 evidence PASS")
PY

stop_processes "${ROLE_PIDS[@]}"
ROLE_PIDS=()
start_role api-a "${PORTS[14]}"; API_A_URL="$ROLE_URL"
start_role api-b "${PORTS[15]}"; API_B_URL="$ROLE_URL"
start_role play-a "${PORTS[16]}"; PLAY_A_URL="$ROLE_URL"
start_role play-b "${PORTS[17]}"; PLAY_B_URL="$ROLE_URL"
start_role session-a "${PORTS[18]}"; SESSION_A_URL="$ROLE_URL"
start_role session-b "${PORTS[19]}"; SESSION_B_URL="$ROLE_URL"
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
python3 - "$LOG_DIR/play-a.draining.evidence.json" <<'PY'
import json
import sys
evidence = json.load(open(sys.argv[1], encoding="utf-8"))
if evidence["ready"]:
    raise SystemExit("OBS-C1: drain did not flip readiness")
if not any(row["draining"] for row in evidence["peerRows"]):
    raise SystemExit("OBS-C1: typed draining peer row missing")
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
if not any(actor in after_actors and after_actors[actor] != node
           for actor, node in before_actors.items()):
    raise SystemExit("OBS-C2: actor location did not move to another node")
if not any(sample["name"] == "zlink.drain.actors.handed_off"
           for sample in after["metrics"]):
    raise SystemExit("OBS-C2: actor handoff metric missing")
PY
wait "$hold_play_pid"
wait "$play_drain_pid"
grep -q '"result":"drained"' "$LOG_DIR/play-a.drain.json"
grep -q "OBS-C2 bound_push=continued target=2202" "$LOG_DIR/hold-play.stdout.log"
grep -q "OBS-C3 hold=released" "$LOG_DIR/hold-play.stdout.log"
echo "OBS-C3 drain-natural PASS"

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
start_role api-a "${PORTS[14]}"; API_A_URL="$ROLE_URL"
start_role api-b "${PORTS[15]}"; API_B_URL="$ROLE_URL"
start_role play-a "${PORTS[16]}"; PLAY_A_URL="$ROLE_URL"
start_role play-b "${PORTS[17]}"; PLAY_B_URL="$ROLE_URL"
start_role session-a "${PORTS[18]}"; SESSION_A_URL="$ROLE_URL"
start_role session-b "${PORTS[19]}"; SESSION_B_URL="$ROLE_URL"
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
