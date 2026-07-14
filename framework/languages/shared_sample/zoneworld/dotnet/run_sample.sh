#!/usr/bin/env bash
# ZoneWorld (dotnet). Brings up the topology in the order §12 fixes, then runs the
# scenario client against it.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCENARIO="${1:-all}"
RUN_ID="$(date +%Y%m%d-%H%M%S)"
LOG_DIR="$ROOT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"

REDIS_PORT="${ZONEWORLD_REDIS_PORT:-6379}"
export ZONEWORLD_REDIS_ENDPOINT="${ZONEWORLD_REDIS_ENDPOINT:-127.0.0.1:$REDIS_PORT}"
export ZONEWORLD_REDIS_KEY_PREFIX="zoneworld-$RUN_ID:"
export ZONEWORLD_LOG_DIR="$LOG_DIR"

PIDS=()

cleanup() {
  for pid in "${PIDS[@]:-}"; do
    kill "$pid" 2>/dev/null || true
  done
  for pid in "${PIDS[@]:-}"; do
    wait "$pid" 2>/dev/null || true
  done
  redis-cli -p "$REDIS_PORT" --scan --pattern "zoneworld-$RUN_ID:*" 2>/dev/null \
    | xargs -r redis-cli -p "$REDIS_PORT" del >/dev/null 2>&1 || true
}
trap cleanup EXIT

# A node left over from an earlier run keeps its routing id bound, so the mesh routes to the
# ghost instead of the node this run just started — every cross-node transfer then vanishes
# and the scenarios fail as if the code were broken. Refuse to start until they are gone.
echo "==> orphan check"
if pgrep -f "bin/Debug/net8.0/ZoneWorld.Server" >/dev/null 2>&1; then
  echo "    a ZoneWorld server from an earlier run is still alive; terminating it"
  pkill -f "bin/Debug/net8.0/ZoneWorld.Server" 2>/dev/null || true
  sleep 3
  pkill -9 -f "bin/Debug/net8.0/ZoneWorld.Server" 2>/dev/null || true
  sleep 1
fi

echo "==> redis check"
redis-cli -p "$REDIS_PORT" ping >/dev/null

echo "==> build"
dotnet build "$ROOT_DIR/Server/Ops/ZoneWorld.Server.Ops.csproj" -v q --nologo >/dev/null
dotnet build "$ROOT_DIR/Server/ZoneNode/ZoneWorld.Server.ZoneNode.csproj" -v q --nologo >/dev/null
dotnet build "$ROOT_DIR/Server/Gateway/ZoneWorld.Server.Gateway.csproj" -v q --nologo >/dev/null
dotnet build "$ROOT_DIR/Client/ZoneWorld.Client.csproj" -v q --nologo >/dev/null

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
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
  unset "NODE_PID[$name]"
  # Give the kernel a moment to release the node's endpoints; a restart that races the old
  # process's sockets fails to bind and looks like a sample bug.
  sleep 3
}

start_zone_node() {
  local name="$1"
  : >"$LOG_DIR/$name.restart.marker"
  start "$name" "$SERVER_BIN" "$name"
  wait_for_log "$name" "topology=ready"
  # The node is up, but its peers still have to notice it: auto-connect and the location rows
  # settle over the next few seconds. Starting the next scenario before that just makes it flaky.
  sleep 12
}

run_client() {
  "$CLIENT_BIN" "$1" 2>&1 | tee -a "$LOG_DIR/client.log"
  return "${PIPESTATUS[0]}"
}

# Runs a client scenario while the runner disrupts the topology underneath it.
run_client_with_stop() {
  local id="$1" node="$2" delay="$3"
  if [[ "$SCENARIO" != "all" && "$SCENARIO" != *"$id"* ]]; then return 0; fi

  run_client "$id" &
  local client_pid=$!
  sleep "$delay"
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

echo "==> ops"
start ops "$OPS_BIN"
sleep 2

echo "==> zone nodes"
# ZW-C4 needs a real spot runtime event, so zone-node-1's zone-nw tick is made to throw
# once. `env` keeps the switch on this one process — the other nodes must stay healthy.
start zone-node-1 env ZONEWORLD_FAULT_TICK_ZONE="${ZONEWORLD_FAULT_TICK_ZONE:-zone-nw}" \
  "$SERVER_BIN" zone-node-1
start zone-node-2 "$SERVER_BIN" zone-node-2
wait_for_log zone-node-1 "topology=ready"
wait_for_log zone-node-2 "topology=ready"

# The third node hosts no zone: it only subscribes to the broadcast channel. It exists to
# show that Ops publishes without a node list — adding a node changes nothing on the
# publishing side (ZW-D2, scenario §11.1).
echo "==> zone-node-3 (fanout subscriber only)"
start zone-node-3 "$SERVER_BIN" zone-node-3
wait_for_log zone-node-3 "topology=ready"

echo "==> gateway"
start gateway "$GATEWAY_BIN"
sleep 3

echo "==> scenarios ($SCENARIO)"
set +e
CLIENT_SCENARIO="$(printf '%s' "$SCENARIO" | tr ',' '\n' | grep -vxE 'ZW-D2|ZW-F2|ZW-C2|ZW-C3|ZW-B4|ZW-E5|ZW-E5-arm' | paste -sd, -)"
if [[ "$SCENARIO" == "all" || -n "$CLIENT_SCENARIO" ]]; then
  "$CLIENT_BIN" \
    "${CLIENT_SCENARIO:-$SCENARIO}" 2>&1 | tee "$LOG_DIR/client.log"
  status=${PIPESTATUS[0]}
else
  status=0
fi
set -e

# Some of what §11 asks for is not observable from a client: the absence of a client (ZW-F2), a
# node the client is never told about (ZW-D2), another node's fanout subscriber (ZW-D1), the
# whole bot population (ZW-F1), or a push that must never be attempted (ZW-F3). The runner reads
# those out of the server logs.
runner_scenario() {
  local id="$1" description="$2" log="$3" pattern="$4"
  if [[ "$SCENARIO" != "all" && "$SCENARIO" != *"$id"* ]]; then return 0; fi
  if grep -q "$pattern" "$LOG_DIR/$log" 2>/dev/null; then
    echo "scenario $id passed"
  else
    echo "scenario $id FAILED: $description" >&2
    status=1
  fi
}

# Passes only when the pattern appears in *every* named log.
runner_scenario_all() {
  local id="$1" description="$2" pattern="$3"; shift 3
  if [[ "$SCENARIO" != "all" && "$SCENARIO" != *"$id"* ]]; then return 0; fi
  local log
  for log in "$@"; do
    if ! grep -q "$pattern" "$LOG_DIR/$log" 2>/dev/null; then
      echo "scenario $id FAILED: $description ($log)" >&2
      status=1
      return 0
    fi
  done
  echo "scenario $id passed"
}

# Passes only when the pattern appears nowhere. An assertion about something that must not
# happen has to be written as an absence, or it asserts nothing.
runner_scenario_absent() {
  local id="$1" description="$2" pattern="$3"; shift 3
  if [[ "$SCENARIO" != "all" && "$SCENARIO" != *"$id"* ]]; then return 0; fi
  local hits
  hits="$(grep -l "$pattern" "${@/#/$LOG_DIR/}" 2>/dev/null || true)"
  if [[ -n "$hits" ]]; then
    echo "scenario $id FAILED: $description ($hits)" >&2
    status=1
  else
    echo "scenario $id passed"
  fi
}

# These all need zone-node-2 to disappear while a client is watching, and each one restarts it
# afterwards. ZW-B4 goes first because it is the only one that has to *use* zone-node-2 before
# taking it away — it walks a player into zone-ne — and a node that has just come back is the
# least reliable thing to walk into.
run_client_with_stop ZW-B4 zone-node-2 20
run_client_with_stop ZW-C2 zone-node-2 6
run_client_with_stop ZW-C3 zone-node-2 6

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
# announcement it receives is never a duplicate.
runner_scenario_all ZW-D1 "a node's fanout subscriber never received the announcement" \
  "fanout subscriber received announcement" zone-node-1.log zone-node-2.log
runner_scenario_all ZW-D1-spots "a zone spot never received the announcement" \
  "zone spot: announcement delivered" zone-node-1.log zone-node-2.log

# ZW-F1: the world holds eight bots. No client sees them all — a client only ever receives one
# zone's view — so the population is counted here.
if [[ "$SCENARIO" == "all" || "$SCENARIO" == *"ZW-F1"* ]]; then
  bots="$(grep -ho "bot spawned. bot=[a-z0-9-]*" "$LOG_DIR"/zone-node-*.log 2>/dev/null \
    | sort -u | wc -l)"
  if [[ "$bots" -eq 8 ]]; then
    echo "scenario ZW-F1-population passed"
  else
    echo "scenario ZW-F1-population FAILED: the world holds $bots bots, not 8" >&2
    status=1
  fi
fi

# ZW-F3: nothing is ever pushed to an actor with no bound session. That is an absence, so it is
# asserted as one: a push to an unbound actor leaves this error behind, and it must not be there.
runner_scenario_absent ZW-F3-no-push "a push was attempted to an actor with no bound session" \
  "No current session binding exists for actor" \
  zone-node-1.log zone-node-2.log zone-node-3.log

# ZW-D2: the third node received the announcement with no change to Ops.
runner_scenario ZW-D2 "zone-node-3 never received a world announcement" \
  zone-node-3.log "fanout subscriber received announcement"

# ZW-F2: a bot crossed to the other node. The bots run with no client attached, so a
# transfer recorded here proves the actor moved without a bound session.
runner_scenario ZW-F2 "no bot transferred across nodes" \
  zone-node-2.log "player entered. zone=zone-ne, player=bot-nw-x, bot=True, from=zone-node-1"

echo "==> logs: $LOG_DIR"
exit "$status"
