#!/usr/bin/env bash
# ZoneWorld (dotnet). Brings up the topology in the order §12 fixes, then runs the
umask 077
# scenario client against it.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$ROOT_DIR/../redis-common.sh"
SCENARIO="${1:-all}"
RUN_DIR="${SAMPLE_RUN_DIR:-$(mktemp -d)}"
RUN_ID="$(basename "$RUN_DIR")-$$-$RANDOM"
LOG_DIR="$RUN_DIR/logs"
CONFIG_DIR="$RUN_DIR/config"
mkdir -p "$LOG_DIR" "$CONFIG_DIR"

PIDS=()
REDIS_CONTAINER=""

cleanup() {
  find "${RUN_DIR}" -type f -name "*.json" -delete 2>/dev/null || true
  for pid in "${PIDS[@]:-}"; do
    # Scenario shutdown semantics are tested explicitly below. EXIT cleanup only owns test
    # processes, so stop them immediately instead of entering every server's bounded drain and
    # making a completed or failed runner wait for several overlapping drain windows.
    kill -KILL "$pid" 2>/dev/null || true
  done
  for pid in "${PIDS[@]:-}"; do
    wait "$pid" 2>/dev/null || true
  done
  if [[ -n "$REDIS_CONTAINER" ]]; then
    docker rm -fv "$REDIS_CONTAINER" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

if ! command -v docker >/dev/null 2>&1; then
  echo "Docker is required to run ZoneWorld (it provisions a dedicated Redis container)." >&2
  exit 1
fi
REDIS_CONTAINER="zlink-zoneworld-dotnet-redis-$RUN_ID"
zlink_redis_start_scoped_assign REDIS_CONTAINER REDIS_ENDPOINT \
  "zlink-zoneworld-dotnet-redis" redis:7.2-alpine

echo "==> build"
dotnet build "$ROOT_DIR/Server/Ops/ZoneWorld.Server.Ops.csproj" -v q --nologo >/dev/null
dotnet build "$ROOT_DIR/Server/ZoneNode/ZoneWorld.Server.ZoneNode.csproj" -v q --nologo >/dev/null
dotnet build "$ROOT_DIR/Server/Gateway/ZoneWorld.Server.Gateway.csproj" -v q --nologo >/dev/null
dotnet build "$ROOT_DIR/Client/ZoneWorld.Client.csproj" -v q --nologo >/dev/null

read -r -a PORTS <<<"$(python3 - <<'PY'
import random
import socket

sockets = []
chosen = set()
try:
    while len(sockets) < 22:
        port = random.randint(41000, 60999)
        if port in chosen:
            continue
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            sock.bind(("127.0.0.1", port))
        except OSError:
            sock.close()
            continue
        chosen.add(port)
        sockets.append(sock)
    print(" ".join(str(sock.getsockname()[1]) for sock in sockets))
finally:
    for sock in sockets:
        sock.close()
PY
)"

GATEWAY_ENDPOINT="ws://127.0.0.1:${PORTS[18]}"
OPS_ENDPOINT="ws://127.0.0.1:${PORTS[15]}"
BROWSER_PREVIEW_PORT="${PORTS[21]}"

python3 - "$CONFIG_DIR" "$REDIS_ENDPOINT" "$RUN_ID" "$LOG_DIR" "${PORTS[@]}" <<'PY'
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
redis = sys.argv[2]
run_id = sys.argv[3]
log_dir = sys.argv[4]
ports = [int(port) for port in sys.argv[5:]]
shared = {"redisEndpoint": redis, "redisKeyPrefix": f"zoneworld-{run_id}:", "logDirectory": log_dir}

def write(name, role, value):
    (root / f"{name}.json").write_text(json.dumps({"shared": shared, role: value}), encoding="utf-8")

for index in (1, 2, 3):
    offset = (index - 1) * 5
    write(f"zone-node-{index}", "zoneNode", {
        "nodeId": f"zone-node-{index}",
        "spotRouterEndpoint": f"tcp://127.0.0.1:{ports[offset]}",
        "spotPubSubEndpoint": f"tcp://127.0.0.1:{ports[offset + 1]}",
        "opsChannelEndpoint": f"tcp://127.0.0.1:{ports[offset + 2]}",
        "actorsChannelEndpoint": f"tcp://127.0.0.1:{ports[offset + 3]}",
        "bridgeEndpoint": f"tcp://127.0.0.1:{ports[offset + 4]}",
        "faultTickZone": "zone-nw" if index == 1 else None,
        "disableBots": False,
    })

write("ops", "ops", {
    "streamEndpoint": f"ws://127.0.0.1:{ports[15]}",
    "broadcastEndpoint": f"tcp://127.0.0.1:{ports[16]}",
    "reportEndpoint": f"tcp://127.0.0.1:{ports[17]}",
})
write("gateway", "gateway", {
    "streamEndpoint": f"ws://127.0.0.1:{ports[18]}",
    "spotRouterEndpoint": f"tcp://127.0.0.1:{ports[19]}",
    "spotPubSubEndpoint": f"tcp://127.0.0.1:{ports[20]}",
})
PY

declare -A NODE_PID

# `dotnet run` is a wrapper: killing it does not always take the server it launched with it,
# and a survivor keeps the node's routing id bound (see the orphan check above). Launch the
# built binary so the pid we record is the server itself.
BIN_DIR="bin/Debug/net8.0"
SERVER_BIN="$ROOT_DIR/Server/ZoneNode/$BIN_DIR/ZoneWorld.Server.ZoneNode"
OPS_BIN="$ROOT_DIR/Server/Ops/$BIN_DIR/ZoneWorld.Server.Ops"
GATEWAY_BIN="$ROOT_DIR/Server/Gateway/$BIN_DIR/ZoneWorld.Server.Gateway"
CLIENT_BIN="$ROOT_DIR/Client/$BIN_DIR/ZoneWorld.Client"

start() {
  local name="$1"; shift
  "$@" >>"$LOG_DIR/$name.log" 2>&1 &
  PIDS+=($!)
  NODE_PID[$name]=$!
  echo "    started $name (pid ${PIDS[-1]})"
}

# ZW-B4·ZW-C2·ZW-E5 assert what happens when a node goes away, so the runner has to take one
# away. The client cannot: it only speaks to Gateway and Ops.
stop_node() {
  local name="$1"
  local pid="${NODE_PID[$name]:-}"
  [[ -n "$pid" ]] || return 0
  echo "    stopping $name (pid $pid)"
  # These scenarios model a node disappearing, not a graceful deployment drain. SIGTERM keeps
  # the framework's owner lease alive while its bounded drain runs (about 30 seconds here), so
  # the node is still registered for most of the client's observation window. An abrupt stop
  # leaves the last lease behind and lets its configured TTL drive the automatic unregister.
  kill -KILL "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
  unset "NODE_PID[$name]"
}

start_zone_node() {
  local name="$1"
  local first_new_line first_new_ops_line
  first_new_line=$(($(wc -l <"$LOG_DIR/$name.log" 2>/dev/null || printf '0') + 1))
  first_new_ops_line=$(($(wc -l <"$LOG_DIR/ops.log" 2>/dev/null || printf '0') + 1))
  : >"$LOG_DIR/$name.restart.marker"
  start "$name" "$SERVER_BIN" --config "$CONFIG_DIR/$name.json"
  # A crashed owner keeps its allocated slot until the 30-second lease expires. Wait for that
  # contract plus reconcile time instead of treating the old three-second test lease as current.
  wait_for_log_after "$name" "topology=ready" "$first_new_line" 450
  # These two independent observations replace a fixed convergence delay: the restarted node
  # has submitted a status report, and Ops has observed the new socket connection.
  wait_for_log_after "$name" "node status report submitted. node=$name" "$first_new_line" 450
  wait_for_log_after ops "node connection observed. node=$name, connected=True" "$first_new_ops_line" 450
  wait_for_log_after ops "ops channel observed. node=$name, connected=True, kind=ConnectionReady" "$first_new_ops_line" 450
}

client_config() {
  local scenarios="$1" path="$CONFIG_DIR/client.json"
  python3 - "$CONFIG_DIR/ops.json" "$path" "$scenarios" "$GATEWAY_ENDPOINT" "$OPS_ENDPOINT" <<'PY'
import json
import pathlib
import sys

source = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
source.pop("ops")
source["client"] = {
    "gatewayEndpoint": sys.argv[4],
    "opsEndpoint": sys.argv[5],
    "scenarios": sys.argv[3],
}
pathlib.Path(sys.argv[2]).write_text(json.dumps(source), encoding="utf-8")
PY
  printf '%s\n' "$path"
}

run_client() {
  local config
  config="$(client_config "$1")"
  "$CLIENT_BIN" --config "$config" 2>&1 | tee -a "$LOG_DIR/client.log"
  return "${PIPESTATUS[0]}"
}

# Runs a client scenario while the runner disrupts the topology underneath it.
run_client_with_stop() {
  local id="$1" node="$2" first_new_line client_pid
  if [[ "$SCENARIO" != "all" && "$SCENARIO" != *"$id"* ]]; then return 0; fi

  if [[ -f "$LOG_DIR/client.log" ]]; then
    first_new_line=$(($(wc -l <"$LOG_DIR/client.log") + 1))
  else
    first_new_line=1
  fi
  run_client "$id" &
  client_pid=$!
  # The client announces that the precondition it must observe is established before the
  # runner removes the node. A fixed delay can kill a loaded node before the client reaches
  # that state, turning a setup race into a false lifecycle failure.
  if ! wait_for_log_after client "scenario $id armed" "$first_new_line" 600; then
    wait "$client_pid" || status=1
    return 0
  fi
  stop_node "$node"
  wait "$client_pid" || status=1
  start_zone_node "$node"
}

wait_for_log() {
  local name="$1" pattern="$2" attempts="${3:-200}"
  for ((i = 0; i < attempts; i++)); do
    if grep -q "$pattern" "$LOG_DIR/$name.log" 2>/dev/null; then return 0; fi
    sleep 0.1
  done
  echo "!! $name never logged '$pattern'" >&2
  tail -20 "$LOG_DIR/$name.log" >&2 || true
  return 1
}

wait_for_log_after() {
  local name="$1" pattern="$2" first_line="$3" attempts="${4:-200}"
  for ((i = 0; i < attempts; i++)); do
    if tail -n +"$first_line" "$LOG_DIR/$name.log" 2>/dev/null | grep -q "$pattern"; then
      return 0
    fi
    sleep 0.1
  done
  echo "!! $name never logged '$pattern' after line $first_line" >&2
  tail -20 "$LOG_DIR/$name.log" >&2 || true
  return 1
}

wait_for_file_while_running() {
  local path="$1" pid="$2" attempts="${3:-450}"
  for ((i = 0; i < attempts; i++)); do
    [[ -f "$path" ]] && return 0
    kill -0 "$pid" 2>/dev/null || return 1
    sleep 0.1
  done
  return 1
}

echo "==> ops"
start ops "$OPS_BIN" --config "$CONFIG_DIR/ops.json"
wait_for_log ops "Application started."

echo "==> zone nodes"
# ZW-C4 needs a real spot runtime event, so zone-node-1's zone-nw tick is made to throw
# once. `env` keeps the switch on this one process — the other nodes must stay healthy.
start zone-node-1 "$SERVER_BIN" --config "$CONFIG_DIR/zone-node-1.json"
start zone-node-2 "$SERVER_BIN" --config "$CONFIG_DIR/zone-node-2.json"
wait_for_log zone-node-1 "topology=ready"
wait_for_log zone-node-2 "topology=ready"

# The third node hosts no zone: it only subscribes to the broadcast channel. It exists to
# show that Ops publishes without a node list — adding a node changes nothing on the
# publishing side (ZW-D2, scenario §11.1).
echo "==> zone-node-3 (fanout subscriber only)"
start zone-node-3 "$SERVER_BIN" --config "$CONFIG_DIR/zone-node-3.json"
wait_for_log zone-node-3 "topology=ready"

echo "==> gateway"
start gateway "$GATEWAY_BIN" --config "$CONFIG_DIR/gateway.json"
wait_for_log gateway "Application started."
wait_for_log ops "ops channel observed. node=zone-node-1, connected=True, kind=ConnectionReady"
wait_for_log ops "ops channel observed. node=zone-node-2, connected=True, kind=ConnectionReady"

echo "==> scenarios ($SCENARIO)"
# The same browser client is shared by every language implementation. Language runners opt in
# to the live browser smoke with one flag; the client receives only Gateway and Ops endpoints.
if [[ "${ZONEWORLD_BROWSER_SMOKE:-0}" == "1" ]]; then
  echo "==> shared browser client"
  browser_marker="$RUN_DIR/browser-lifecycle-armed"
  ZONEWORLD_GATEWAY="$GATEWAY_ENDPOINT" \
  ZONEWORLD_OPS="$OPS_ENDPOINT" \
  ZONEWORLD_BROWSER_PREVIEW_PORT="$BROWSER_PREVIEW_PORT" \
  ZONEWORLD_BROWSER_LIFECYCLE_MARKER="$browser_marker" \
    npm --prefix "$ROOT_DIR/../../../shared_sample/zoneworld/client" run test:e2e:live &
  browser_pid=$!
  browser_node_stopped=0

  if wait_for_file_while_running "$browser_marker" "$browser_pid"; then
    stop_node zone-node-2
    browser_node_stopped=1
  else
    echo "!! browser client did not arm its node lifecycle check" >&2
  fi

  set +e
  wait "$browser_pid"
  browser_status=$?
  set -e
  if [[ "$browser_node_stopped" -eq 1 ]]; then
    start_zone_node zone-node-2
  else
    browser_status=1
  fi
  if [[ "$browser_status" -ne 0 ]]; then
    echo "!! shared browser client failed" >&2
    exit "$browser_status"
  fi
fi
set +e
CLIENT_SCENARIO="$(printf '%s' "$SCENARIO" | tr ',' '\n' | grep -vxE 'ZW-D2|ZW-F2|ZW-C2|ZW-C3|ZW-B4|ZW-E5|ZW-E5-arm' | paste -sd, -)"
if [[ "$SCENARIO" == "all" || -n "$CLIENT_SCENARIO" ]]; then
  CLIENT_CONFIG="$(client_config "${CLIENT_SCENARIO:-$SCENARIO}")"
  "$CLIENT_BIN" --config "$CLIENT_CONFIG" 2>&1 | tee "$LOG_DIR/client.log"
  status=${PIPESTATUS[0]}
else
  status=0
fi
set -e

# Some of what §11 asks for is not observable from a client: the absence of a client (ZW-F2), a
# node the client is never told about (ZW-D2), another node's fanout subscriber (ZW-D1), the
# whole bot population (ZW-F1), or a push that must never be attempted (ZW-F3). The runner reads
# those out of the server logs.
# The client writes its verdicts to client.log; these write theirs here. The §12 markers below
# are decided from both, so a runner verdict has to be recorded, not just printed.
RUNNER_LOG="$LOG_DIR/runner.log"
: >"$RUNNER_LOG"

pass() { echo "scenario $1 passed" | tee -a "$RUNNER_LOG"; }
fail() { echo "scenario $1 FAILED: $2" >&2; echo "scenario $1 failed" >>"$RUNNER_LOG"; status=1; }

# A runner verdict such as ZW-D1-spots belongs to ZW-D1: it asserts the half of that scenario a
# client cannot see. Selecting ZW-D1 has to select it too, or a targeted run quietly proves less
# than the same scenario proves in a full one.
selects() {
  [[ "$SCENARIO" == "all" ]] && return 0
  local base
  base="$(printf '%s' "$1" | cut -d- -f1,2)"
  [[ "$SCENARIO" == *"$base"* ]]
}

runner_scenario() {
  local id="$1" description="$2" log="$3" pattern="$4"
  selects "$id" || return 0
  if grep -q "$pattern" "$LOG_DIR/$log" 2>/dev/null; then
    pass "$id"
  else
    fail "$id" "$description"
  fi
}

# Passes only when the pattern appears in *every* named log.
runner_scenario_all() {
  local id="$1" description="$2" pattern="$3"; shift 3
  selects "$id" || return 0
  local log
  for log in "$@"; do
    if ! grep -q "$pattern" "$LOG_DIR/$log" 2>/dev/null; then
      fail "$id" "$description ($log)"
      return 0
    fi
  done
  pass "$id"
}

# Passes only when the pattern appears nowhere. An assertion about something that must not
# happen has to be written as an absence, or it asserts nothing.
runner_scenario_absent() {
  local id="$1" description="$2" pattern="$3"; shift 3
  selects "$id" || return 0
  local hits
  hits="$(grep -l "$pattern" "${@/#/$LOG_DIR/}" 2>/dev/null || true)"
  if [[ -n "$hits" ]]; then
    fail "$id" "$description ($hits)"
  else
    pass "$id"
  fi
}

# These all need zone-node-2 to disappear while a client is watching, and each one restarts it
# afterwards. ZW-B4 goes first because it is the only one that has to *use* zone-node-2 before
# taking it away — it walks a player into zone-ne — and a node that has just come back is the
# least reliable thing to walk into.
run_client_with_stop ZW-B4 zone-node-2
run_client_with_stop ZW-C2 zone-node-2
run_client_with_stop ZW-C3 zone-node-2

# ZW-E5: the operator closes a node, the node restarts, and it comes back still closed —
# the desired state lives in the store, not in a message.
if [[ "$SCENARIO" == "all" || "$SCENARIO" == *"ZW-E5"* ]]; then
  run_client ZW-E5-arm || status=1
  stop_node zone-node-2
  start_zone_node zone-node-2
  run_client ZW-E5 || status=1
fi

# ZW-D1: one publish, no node list, and it comes out of *both* nodes' fanout subscribers and
# reaches *every* zone spot. The client half of ZW-D1 asserts what a client can see — that an
# announcement it receives is never a duplicate — so these carry their own ids: a client verdict
# and a runner verdict on the same id would be indistinguishable in the log.
runner_scenario_all ZW-D1-subscribers "a node's fanout subscriber never received the announcement" \
  "fanout subscriber received announcement" zone-node-1.log zone-node-2.log
runner_scenario_all ZW-D1-spots "a zone spot never received the announcement" \
  "zone spot: announcement delivered" zone-node-1.log zone-node-2.log

# ZW-F1: the world holds eight bots. No client sees them all — a client only ever receives one
# zone's view — so the population is counted here.
if selects ZW-F1-population; then
  bots="$(grep -ho "bot spawned. bot=[a-z0-9-]*" "$LOG_DIR"/zone-node-*.log 2>/dev/null \
    | sort -u | wc -l)"
  if [[ "$bots" -eq 8 ]]; then
    pass ZW-F1-population
  else
    fail ZW-F1-population "the world holds $bots bots, not 8"
  fi
fi

# ZW-F3: a bot has no bound session, so nothing is ever pushed to one (§11 ZW-F3). That is an
# absence, asserted as one — a push to a bot would leave this error behind with the bot's id.
#
# The id matters. The same error also fires, benignly, for a *human* whose client disconnected in
# the tick before the spot dropped them: the spot catches it and moves on by design (§8.9.2, and
# ZoneSpot.PushToClients). That is a different, known case — not what ZW-F3 is about — so the
# check names bot ids ("bot-...") only. If the bot exclusion ever broke, the push would carry a
# bot id and this would catch it; a human mid-disconnect does not.
runner_scenario_absent ZW-F3-no-push "a push was attempted to a bot (an actor with no bound session)" \
  "No current session binding exists for actor 'bot-" \
  zone-node-1.log zone-node-2.log zone-node-3.log

# ZW-D2: the third node received the announcement with no change to Ops.
runner_scenario ZW-D2 "zone-node-3 never received a world announcement" \
  zone-node-3.log "fanout subscriber received announcement"

# ZW-F2: a bot crossed to the other node. The bots run with no client attached, so a
# transfer recorded here proves the actor moved without a bound session.
runner_scenario ZW-F2 "no bot transferred across nodes" \
  zone-node-2.log "player entered. zone=zone-ne, player=bot-nw-x, bot=True, from=zone-node-1"

# §12 asks the sample to say what it proved, not just that it exited zero. The runner owns these
# markers because no single client run sees the whole suite: the client's batch leaves out the
# scenarios that need a node taken away, and six more are judged from server logs. A marker is
# printed only when every scenario behind it actually passed — a phase that says "completed"
# without having run is worse than no marker at all.
declare -A PASSED=()
while read -r id; do PASSED["$id"]=1; done < <(
  sed -nE 's/^scenario ([A-Za-z0-9-]+) passed$/\1/p' \
    "$LOG_DIR/client.log" "$RUNNER_LOG" 2>/dev/null
)

phase() {
  local marker="$1"; shift
  local id
  for id in "$@"; do
    if [[ -z "${PASSED[$id]:-}" ]]; then
      echo "!! $marker withheld: $id did not pass" >&2
      return 0
    fi
  done
  echo "$marker"
}

# Only a full run can claim a phase. A selective run proves one scenario, not a capability.
if [[ "$SCENARIO" == "all" ]]; then
  phase "zoneworld-transfer=completed"        ZW-B2 ZW-B3 ZW-F2
  phase "zoneworld-border-sync=completed"     ZW-B1 ZW-B4
  phase "zoneworld-ops-observe=completed"     ZW-C1 ZW-C2 ZW-C3 ZW-C4
  phase "zoneworld-ops-announce=completed"    ZW-D1 ZW-D1-subscribers ZW-D1-spots ZW-D2
  phase "zoneworld-ops-maintenance=completed" ZW-E1 ZW-E2 ZW-E3 ZW-E4 ZW-E5 ZW-E6

  # The whole of §11, plus the six verdicts only the runner can reach. Gated on status as well:
  # a scenario can fail without its id ever reaching the log.
  phase "zoneworld=completed" \
    ZW-A1 ZW-A2 ZW-A3 ZW-A4 ZW-A5 \
    ZW-B1 ZW-B2 ZW-B3 ZW-B4 \
    ZW-C1 ZW-C2 ZW-C3 ZW-C4 \
    ZW-D1 ZW-D1-subscribers ZW-D1-spots ZW-D2 \
    ZW-E1 ZW-E2 ZW-E3 ZW-E4 ZW-E5 ZW-E5-arm ZW-E6 \
    ZW-F1 ZW-F1-population ZW-F2 ZW-F3 ZW-F3-no-push ZW-F4
fi

echo "==> logs: $LOG_DIR"
exit "$status"
