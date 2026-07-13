#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"
RUN_DIR="$(mktemp -d)"
LOG_DIR="${RUN_DIR}/logs"
export BINGO_LOG_DIR="${BINGO_LOG_DIR:-${LOG_DIR}}"
mkdir -p "${LOG_DIR}" "${BINGO_LOG_DIR}"
rm -f "${BINGO_LOG_DIR}"/*.log
(cd "${SCRIPT_DIR}" && npm run build >/dev/null)

PIDS=()
PID_NAMES=()
LAST_SERVER_PID=""
PROBE_PID=""
REDIS_CONTAINER_ID=""
source "${SCRIPT_DIR}/../../e2e/redis-container.sh"

cleanup() {
  local status="$?"
  set +e
  if [[ -n "${PROBE_PID}" ]] && kill -0 "${PROBE_PID}" 2>/dev/null; then
    kill -INT "${PROBE_PID}" 2>/dev/null || true
  fi
  for ((i=${#PIDS[@]}-1; i>=0; i--)); do
    local pid="${PIDS[$i]}"
    if kill -0 "${pid}" 2>/dev/null; then
      kill -INT "${pid}" 2>/dev/null || true
    fi
  done
  for _ in $(seq 1 20); do
    local any_alive=0
    for pid in "${PIDS[@]}"; do
      if kill -0 "${pid}" 2>/dev/null; then
        any_alive=1
        break
      fi
    done
    if [[ "${any_alive}" == "0" ]]; then
      break
    fi
    sleep 0.1
  done
  for ((i=${#PIDS[@]}-1; i>=0; i--)); do
    local pid="${PIDS[$i]}"
    if kill -0 "${pid}" 2>/dev/null; then
      kill -9 "${pid}" 2>/dev/null || true
    fi
  done
  for pid in "${PIDS[@]}"; do
    wait "${pid}" 2>/dev/null || true
  done
  if [[ -n "${REDIS_CONTAINER_ID}" ]]; then
    timeout -k 2s 10s docker rm -fv "${REDIS_CONTAINER_ID}" >/dev/null 2>&1 || true
  fi
  if [[ "${BINGO_TS_KEEP_RUN_DIR:-}" != "1" ]]; then
    rm -rf "${RUN_DIR}"
  else
    echo "runDir=${RUN_DIR}"
  fi
  return "$status"
}
trap cleanup EXIT

read -r -a PORTS <<<"$(python3 - <<'PY'
import random
import socket

sockets = []
try:
    chosen = set()
    while len(sockets) < 18:
        port = random.randint(48000, 60999)
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

export BINGO_SESSION_A_ENDPOINT="${BINGO_SESSION_A_ENDPOINT:-ws://127.0.0.1:${PORTS[2]}}"
export BINGO_SESSION_A_ROUTE_ENDPOINT="${BINGO_SESSION_A_ROUTE_ENDPOINT:-tcp://127.0.0.1:${PORTS[3]}}"
export BINGO_SESSION_A_SPOT_ENDPOINT="${BINGO_SESSION_A_SPOT_ENDPOINT:-tcp://127.0.0.1:${PORTS[4]}}"
export BINGO_SESSION_A_SPOT_NODE_RID="${BINGO_SESSION_A_SPOT_NODE_RID:-bingo-session-node-a}"
export BINGO_SESSION_B_ENDPOINT="${BINGO_SESSION_B_ENDPOINT:-ws://127.0.0.1:${PORTS[5]}}"
export BINGO_SESSION_B_ROUTE_ENDPOINT="${BINGO_SESSION_B_ROUTE_ENDPOINT:-tcp://127.0.0.1:${PORTS[6]}}"
export BINGO_SESSION_B_SPOT_ENDPOINT="${BINGO_SESSION_B_SPOT_ENDPOINT:-tcp://127.0.0.1:${PORTS[7]}}"
export BINGO_SESSION_B_SPOT_NODE_RID="${BINGO_SESSION_B_SPOT_NODE_RID:-bingo-session-node-b}"
export BINGO_PLAY_A_ENDPOINT="${BINGO_PLAY_A_ENDPOINT:-tcp://127.0.0.1:${PORTS[8]}}"
export BINGO_PLAY_A_ROUTE_ENDPOINT="${BINGO_PLAY_A_ROUTE_ENDPOINT:-tcp://127.0.0.1:${PORTS[9]}}"
export BINGO_PLAY_A_SPOT_ENDPOINT="${BINGO_PLAY_A_SPOT_ENDPOINT:-tcp://127.0.0.1:${PORTS[10]}}"
export BINGO_PLAY_A_SPOT_PUBSUB_ENDPOINT="${BINGO_PLAY_A_SPOT_PUBSUB_ENDPOINT:-tcp://127.0.0.1:${PORTS[11]}}"
export BINGO_PLAY_A_SPOT_NODE_RID="${BINGO_PLAY_A_SPOT_NODE_RID:-bingo-play-node-a}"
export BINGO_PLAY_B_ENDPOINT="${BINGO_PLAY_B_ENDPOINT:-tcp://127.0.0.1:${PORTS[12]}}"
export BINGO_PLAY_B_ROUTE_ENDPOINT="${BINGO_PLAY_B_ROUTE_ENDPOINT:-tcp://127.0.0.1:${PORTS[13]}}"
export BINGO_PLAY_B_SPOT_ENDPOINT="${BINGO_PLAY_B_SPOT_ENDPOINT:-tcp://127.0.0.1:${PORTS[14]}}"
export BINGO_PLAY_B_SPOT_PUBSUB_ENDPOINT="${BINGO_PLAY_B_SPOT_PUBSUB_ENDPOINT:-tcp://127.0.0.1:${PORTS[15]}}"
export BINGO_PLAY_B_SPOT_NODE_RID="${BINGO_PLAY_B_SPOT_NODE_RID:-bingo-play-node-b}"
export BINGO_API_A_ENDPOINT="${BINGO_API_A_ENDPOINT:-tcp://127.0.0.1:${PORTS[16]}}"
export BINGO_API_B_ENDPOINT="${BINGO_API_B_ENDPOINT:-tcp://127.0.0.1:${PORTS[17]}}"
export BINGO_REDIS_KEY_PREFIX="${BINGO_REDIS_KEY_PREFIX:-bingo:node:${RANDOM}:$$:}"
CLIENT_CONFIG="${RUN_DIR}/client.config.json"
API_A_CONFIG="${RUN_DIR}/api-a.config.json"
API_B_CONFIG="${RUN_DIR}/api-b.config.json"
PLAY_A_CONFIG="${RUN_DIR}/play-a.config.json"
PLAY_B_CONFIG="${RUN_DIR}/play-b.config.json"
SESSION_A_CONFIG="${RUN_DIR}/session-a.config.json"
SESSION_B_CONFIG="${RUN_DIR}/session-b.config.json"

# The sample owns its Redis: always provision a dedicated, throwaway container
# so room-allocation state stays isolated per run and never touches a developer's
# local Redis. (BINGO_REDIS_ENDPOINT is intentionally derived here, not read.)
if ! command -v docker >/dev/null 2>&1; then
  echo "Docker is required to run the Bingo sample (it provisions a dedicated Redis container)." >&2
  exit 1
fi
start_redis_container "zlink-redis-node-bingo-${RANDOM}-$$" -p "127.0.0.1::6379" "${ZLINK_REDIS_IMAGE:-redis:7.2-alpine}"
export BINGO_REDIS_ENDPOINT="$(redis_container_endpoint "${REDIS_CONTAINER_ID}")"

python3 - \
  "${CLIENT_CONFIG}" \
  "${API_A_CONFIG}" \
  "${API_B_CONFIG}" \
  "${PLAY_A_CONFIG}" \
  "${PLAY_B_CONFIG}" \
  "${SESSION_A_CONFIG}" \
  "${SESSION_B_CONFIG}" <<PY
import json
import sys

def write(path, sample):
    with open(path, "w", encoding="utf-8") as output:
        json.dump({"sample": sample}, output, indent=2)

base = {
    "redisEndpoint": "${BINGO_REDIS_ENDPOINT}",
    "redisKeyPrefix": "${BINGO_REDIS_KEY_PREFIX}"
}

write(sys.argv[1], {
    **base,
    "sessionAEndpoint": "${BINGO_SESSION_A_ENDPOINT}",
    "sessionBEndpoint": "${BINGO_SESSION_B_ENDPOINT}"
})
write(sys.argv[2], {
    **base,
    "apiEndpoint": "${BINGO_API_A_ENDPOINT}"
})
write(sys.argv[3], {
    **base,
    "apiEndpoint": "${BINGO_API_B_ENDPOINT}"
})
write(sys.argv[4], {
    **base,
    "playEndpoint": "${BINGO_PLAY_A_ENDPOINT}",
    "playRouteEndpoint": "${BINGO_PLAY_A_ROUTE_ENDPOINT}",
    "playSpotEndpoint": "${BINGO_PLAY_A_SPOT_ENDPOINT}",
    "playSpotPubSubEndpoint": "${BINGO_PLAY_A_SPOT_PUBSUB_ENDPOINT}",
    "playSpotNodeRid": "${BINGO_PLAY_A_SPOT_NODE_RID}"
})
write(sys.argv[5], {
    **base,
    "playEndpoint": "${BINGO_PLAY_B_ENDPOINT}",
    "playRouteEndpoint": "${BINGO_PLAY_B_ROUTE_ENDPOINT}",
    "playSpotEndpoint": "${BINGO_PLAY_B_SPOT_ENDPOINT}",
    "playSpotPubSubEndpoint": "${BINGO_PLAY_B_SPOT_PUBSUB_ENDPOINT}",
    "playSpotNodeRid": "${BINGO_PLAY_B_SPOT_NODE_RID}"
})
write(sys.argv[6], {
    **base,
    "sessionEndpoint": "${BINGO_SESSION_A_ENDPOINT}",
    "sessionRouteEndpoint": "${BINGO_SESSION_A_ROUTE_ENDPOINT}",
    "sessionSpotEndpoint": "${BINGO_SESSION_A_SPOT_ENDPOINT}",
    "sessionSpotNodeRid": "${BINGO_SESSION_A_SPOT_NODE_RID}",
    "preferredPlayNodeRid": "${BINGO_PLAY_A_SPOT_NODE_RID}"
})
write(sys.argv[7], {
    **base,
    "sessionEndpoint": "${BINGO_SESSION_B_ENDPOINT}",
    "sessionRouteEndpoint": "${BINGO_SESSION_B_ROUTE_ENDPOINT}",
    "sessionSpotEndpoint": "${BINGO_SESSION_B_SPOT_ENDPOINT}",
    "sessionSpotNodeRid": "${BINGO_SESSION_B_SPOT_NODE_RID}",
    "preferredPlayNodeRid": "${BINGO_PLAY_B_SPOT_NODE_RID}"
})
PY

endpoint_host() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#ws://}"
  echo "${endpoint%:*}"
}

endpoint_port() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#ws://}"
  echo "${endpoint##*:}"
}

wait_port() {
  local name="$1"
  local endpoint="$2"
  local host
  local port
  host="$(endpoint_host "${endpoint}")"
  port="$(endpoint_port "${endpoint}")"
  for _ in $(seq 1 300); do
    check_servers
    if (echo >"/dev/tcp/${host}/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name} at ${endpoint}" >&2
  return 1
}

wait_log_marker() {
  local name="$1"
  local marker="$2"
  local log_file="${LOG_DIR}/${name}.log"
  for _ in $(seq 1 300); do
    check_servers
    if grep -Fq "${marker}" "${log_file}"; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name} readiness marker: ${marker}" >&2
  return 1
}

wait_location_ready() {
  check_servers
  node "${SCRIPT_DIR}/../../e2e/location-readiness.js" \
    --redis-endpoint "${BINGO_REDIS_ENDPOINT}" \
    --key-prefix "${BINGO_REDIS_KEY_PREFIX}location" \
    --peer client-server bingo.api router \
      "${BINGO_API_A_ENDPOINT}" \
      "${BINGO_API_B_ENDPOINT}" \
    --peer spot-mesh bingo.room spot \
      "${BINGO_PLAY_A_SPOT_ENDPOINT}" \
      "${BINGO_PLAY_B_SPOT_ENDPOINT}"
}

start_server() {
  local name="$1"
  local entry="$2"
  local config="$3"
  ZLINK_SAMPLE_CONFIG="${config}" node "${SCRIPT_DIR}/${entry}" >"${LOG_DIR}/${name}.log" 2>&1 &
  LAST_SERVER_PID="$!"
  PIDS+=("${LAST_SERVER_PID}")
  PID_NAMES+=("${name}")
}

check_servers() {
  local index
  for index in "${!PIDS[@]}"; do
    local pid="${PIDS[$index]}"
    if ! kill -0 "${pid}" 2>/dev/null; then
      local status
      set +e
      wait "${pid}" 2>/dev/null
      status="$?"
      set -e
      echo "Bingo server ${PID_NAMES[$index]} exited before readiness with status ${status}" >&2
      if [[ -f "${LOG_DIR}/${PID_NAMES[$index]}.log" ]]; then
        cat "${LOG_DIR}/${PID_NAMES[$index]}.log" >&2
      fi
      return "${status}"
    fi
  done
}

wait_port redis "tcp://${BINGO_REDIS_ENDPOINT}"

start_server api-a dist/Server/Api/main.js "${API_A_CONFIG}"
wait_port api-a "${BINGO_API_A_ENDPOINT}"

start_server api-b dist/Server/Api/main.js "${API_B_CONFIG}"
wait_port api-b "${BINGO_API_B_ENDPOINT}"

start_server play-a dist/Server/Play/main.js "${PLAY_A_CONFIG}"
PLAY_A_PID="${LAST_SERVER_PID}"
wait_port play-a-route "${BINGO_PLAY_A_ROUTE_ENDPOINT}"
wait_port play-a-spot "${BINGO_PLAY_A_SPOT_ENDPOINT}"
wait_port play-a-spot-pubsub "${BINGO_PLAY_A_SPOT_PUBSUB_ENDPOINT}"
wait_log_marker play-a '"event":"ready"'

start_server play-b dist/Server/Play/main.js "${PLAY_B_CONFIG}"
wait_port play-b-route "${BINGO_PLAY_B_ROUTE_ENDPOINT}"
wait_port play-b-spot "${BINGO_PLAY_B_SPOT_ENDPOINT}"
wait_port play-b-spot-pubsub "${BINGO_PLAY_B_SPOT_PUBSUB_ENDPOINT}"

start_server session-a dist/Server/Session/main.js "${SESSION_A_CONFIG}"
wait_port session-a "${BINGO_SESSION_A_ENDPOINT}"
wait_port session-a-route "${BINGO_SESSION_A_ROUTE_ENDPOINT}"
wait_port session-a-spot "${BINGO_SESSION_A_SPOT_ENDPOINT}"

start_server session-b dist/Server/Session/main.js "${SESSION_B_CONFIG}"
wait_port session-b "${BINGO_SESSION_B_ENDPOINT}"
wait_port session-b-route "${BINGO_SESSION_B_ROUTE_ENDPOINT}"
wait_port session-b-spot "${BINGO_SESSION_B_SPOT_ENDPOINT}"
wait_location_ready

PROBE_GATE="${RUN_DIR}/drain-probe.gate"
ZLINK_SAMPLE_CONFIG="${CLIENT_CONFIG}" \
  BINGO_DRAIN_EXCLUDED_NODE_RID="${BINGO_PLAY_A_SPOT_NODE_RID}" \
  BINGO_DRAIN_GATE_FILE="${PROBE_GATE}" \
  node "${SCRIPT_DIR}/../../scripts/browser-e2e/run-sample.mjs" Bingo.Ts drain-match-probe.ts >"${LOG_DIR}/drain-probe.log" 2>&1 &
PROBE_PID="$!"
for _ in $(seq 1 300); do
  if grep -q "bingo-drain-probe ready" "${LOG_DIR}/drain-probe.log"; then break; fi
  kill -0 "${PROBE_PID}" 2>/dev/null
  sleep 0.05
done
grep -q "bingo-drain-probe ready" "${LOG_DIR}/drain-probe.log"

node "${SCRIPT_DIR}/../../scripts/browser-e2e/run-sample.mjs" Bingo.Ts >"${LOG_DIR}/client.log" 2>&1 &
CLIENT_PID="$!"
for _ in $(seq 1 300); do
  check_servers
  if grep -Rhq "bingo-lifecycle timer-started" "${LOG_DIR}"; then
    break
  fi
  if ! kill -0 "${CLIENT_PID}" 2>/dev/null; then
    wait "${CLIENT_PID}"
    echo "Bingo client exited before the drain checkpoint." >&2
    exit 1
  fi
  sleep 0.05
done
grep -Rhq "bingo-lifecycle timer-started" "${LOG_DIR}"
kill -USR2 "${PLAY_A_PID}"
for _ in $(seq 1 300); do
  check_servers
  if grep -q 'zlink metric name=zlink.drain.state value=1 attributes={"state":"draining"}' "${LOG_DIR}/play-a.log"; then
    break
  fi
  sleep 0.05
done
grep -q 'zlink metric name=zlink.drain.state value=1 attributes={"state":"draining"}' "${LOG_DIR}/play-a.log"
touch "${PROBE_GATE}"
wait "${PROBE_PID}"
grep -q "bingo-drain-probe .* excluded=${BINGO_PLAY_A_SPOT_NODE_RID}" "${LOG_DIR}/drain-probe.log"
wait "${CLIENT_PID}"

for _ in $(seq 1 300); do
  if grep -q "bingo-drain result=drained" "${LOG_DIR}/play-a.log" \
    && grep -q "zlink metric name=zlink.drain.actors.handed_off" "${LOG_DIR}/play-a.log"; then
    break
  fi
  sleep 0.05
done

for _ in $(seq 1 100); do
  check_servers
  if grep -Rq "bingo-lifecycle session-disconnect actor=observer destroy=false" "${LOG_DIR}" \
    && grep -Rq "bingo-lifecycle session-disconnect actor=player-1 destroy=false" "${LOG_DIR}" \
    && grep -Rq "bingo-lifecycle session-disconnect actor=player-2 destroy=false" "${LOG_DIR}" \
    && grep -Rq "bingo-lifecycle entry-destroy-complete actor=player-1" "${LOG_DIR}" \
    && grep -Rq "bingo-lifecycle entry-destroy-complete actor=player-2" "${LOG_DIR}"; then
    break
  fi
  sleep 0.05
done

grep -q "stream-inbound sample=Bingo" "${LOG_DIR}/client.log"
grep -Eq "stream-inbound sample=Bingo .* seq=[0-9]" "${LOG_DIR}/client.log"
grep -Eq "stream-inbound sample=Bingo .* name=.*Notify" "${LOG_DIR}/client.log"
grep -q "client=player-1" "${LOG_DIR}/client.log"
grep -q "client=player-2" "${LOG_DIR}/client.log"
grep -q "client=observer" "${LOG_DIR}/client.log"
grep -q "name=BingoRewardAnnouncedNotify" "${LOG_DIR}/client.log"
grep -Rq "message flow" "${BINGO_LOG_DIR}"
grep -Rq "origin=Timer" "${BINGO_LOG_DIR}"
node "${SCRIPT_DIR}/scripts/verify-flow-evidence.js" "${LOG_DIR}"
grep -Rhq "zlink metric name=zlink.stream.connections.active" "${LOG_DIR}"
grep -Rhq "zlink metric name=zlink.spot.queue.depth .*attributes=.*\"kind\":\"user\"" "${LOG_DIR}"
grep -Rhq "zlink metric name=zlink.actor.transfers" "${LOG_DIR}"
grep -q "bingo-drain result=drained" "${LOG_DIR}/play-a.log"
grep -q "zlink metric name=zlink.drain.actors.handed_off" "${LOG_DIR}/play-a.log"
[[ "$(grep -RhF "bingo-lifecycle timer-started" "${LOG_DIR}" | wc -l)" == "1" ]]
for actor in player-1 player-2; do
  [[ "$(grep -RhF "bingo-lifecycle room-leave actor=${actor}" "${LOG_DIR}" | wc -l)" == "1" ]]
  [[ "$(grep -RhF "bingo-lifecycle entry-leave actor=${actor}" "${LOG_DIR}" | wc -l)" == "1" ]]
  [[ "$(grep -RhF "bingo-lifecycle entry-destroy-complete actor=${actor}" "${LOG_DIR}" | wc -l)" == "1" ]]
  [[ "$(grep -RhF "bingo-lifecycle session-disconnect actor=${actor} destroy=false" "${LOG_DIR}" | wc -l)" == "1" ]]
done
[[ "$(grep -RhF "bingo-lifecycle room-leave actor=observer" "${LOG_DIR}" | wc -l)" == "1" ]]
grep -Rhq "bingo-lifecycle entry-joined actor=observer destroy=false" "${LOG_DIR}"
[[ "$(grep -RhF "bingo-lifecycle session-disconnect actor=observer destroy=false" "${LOG_DIR}" | wc -l)" == "1" ]]
if grep -REq "handlerException|phase=error|dispatch error" "${LOG_DIR}" "${BINGO_LOG_DIR}"; then
  echo "Bingo emitted a dispatch error." >&2
  grep -RE "handlerException|phase=error|dispatch error" "${LOG_DIR}" "${BINGO_LOG_DIR}" >&2 || true
  exit 1
fi
echo "bingo=completed"
echo "PASS Bingo.Ts"
