#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"
RUN_DIR="$(mktemp -d)"
LOG_DIR="${RUN_DIR}/logs"
export BINGO_LOG_DIR="${BINGO_LOG_DIR:-${SCRIPT_DIR}/logs}"
mkdir -p "${LOG_DIR}" "${BINGO_LOG_DIR}"
rm -f "${BINGO_LOG_DIR}"/*.log

PIDS=()
REDIS_CONTAINER_ID=""

cleanup() {
  local status="$?"
  set +e
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
    docker rm -f "${REDIS_CONTAINER_ID}" >/dev/null 2>&1 || true
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

export BINGO_SESSION_A_ENDPOINT="${BINGO_SESSION_A_ENDPOINT:-tcp://127.0.0.1:${PORTS[2]}}"
export BINGO_SESSION_A_ROUTE_ENDPOINT="${BINGO_SESSION_A_ROUTE_ENDPOINT:-tcp://127.0.0.1:${PORTS[3]}}"
export BINGO_SESSION_A_SPOT_ENDPOINT="${BINGO_SESSION_A_SPOT_ENDPOINT:-tcp://127.0.0.1:${PORTS[4]}}"
export BINGO_SESSION_A_SPOT_NODE_RID="${BINGO_SESSION_A_SPOT_NODE_RID:-bingo-session-node-a}"
export BINGO_SESSION_B_ENDPOINT="${BINGO_SESSION_B_ENDPOINT:-tcp://127.0.0.1:${PORTS[5]}}"
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
REDIS_CONTAINER_ID="$(docker run -d --rm --name "bingo-node-redis-${RANDOM}-$$" -p "127.0.0.1::6379" redis:7.2-alpine)"
export BINGO_REDIS_ENDPOINT="$(docker port "${REDIS_CONTAINER_ID}" 6379/tcp | sed -E 's/.*:([0-9]+)$/127.0.0.1:\1/')"

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
    "apiEndpoint": "${BINGO_API_A_ENDPOINT}",
    "apiNodeRid": "bingo-api-node-a",
    "playRouteEndpoints": [
        "${BINGO_PLAY_A_ROUTE_ENDPOINT}",
        "${BINGO_PLAY_B_ROUTE_ENDPOINT}"
    ]
})
write(sys.argv[3], {
    **base,
    "apiEndpoint": "${BINGO_API_B_ENDPOINT}",
    "apiNodeRid": "bingo-api-node-b",
    "playRouteEndpoints": [
        "${BINGO_PLAY_A_ROUTE_ENDPOINT}",
        "${BINGO_PLAY_B_ROUTE_ENDPOINT}"
    ]
})
write(sys.argv[4], {
    **base,
    "playEndpoint": "${BINGO_PLAY_A_ENDPOINT}",
    "playRouteEndpoint": "${BINGO_PLAY_A_ROUTE_ENDPOINT}",
    "routePeerEndpoints": [
        "${BINGO_PLAY_B_ROUTE_ENDPOINT}",
        "${BINGO_SESSION_A_ROUTE_ENDPOINT}",
        "${BINGO_SESSION_B_ROUTE_ENDPOINT}"
    ],
    "playSpotEndpoint": "${BINGO_PLAY_A_SPOT_ENDPOINT}",
    "playSpotPubSubEndpoint": "${BINGO_PLAY_A_SPOT_PUBSUB_ENDPOINT}",
    "playSpotNodeRid": "${BINGO_PLAY_A_SPOT_NODE_RID}"
})
write(sys.argv[5], {
    **base,
    "playEndpoint": "${BINGO_PLAY_B_ENDPOINT}",
    "playRouteEndpoint": "${BINGO_PLAY_B_ROUTE_ENDPOINT}",
    "routePeerEndpoints": [
        "${BINGO_PLAY_A_ROUTE_ENDPOINT}",
        "${BINGO_SESSION_A_ROUTE_ENDPOINT}",
        "${BINGO_SESSION_B_ROUTE_ENDPOINT}"
    ],
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
    "preferredPlayNodeRid": "${BINGO_PLAY_A_SPOT_NODE_RID}",
    "preferredPlayRouteEndpoint": "${BINGO_PLAY_A_ROUTE_ENDPOINT}"
})
write(sys.argv[7], {
    **base,
    "sessionEndpoint": "${BINGO_SESSION_B_ENDPOINT}",
    "sessionRouteEndpoint": "${BINGO_SESSION_B_ROUTE_ENDPOINT}",
    "sessionSpotEndpoint": "${BINGO_SESSION_B_SPOT_ENDPOINT}",
    "sessionSpotNodeRid": "${BINGO_SESSION_B_SPOT_NODE_RID}",
    "preferredPlayNodeRid": "${BINGO_PLAY_B_SPOT_NODE_RID}",
    "preferredPlayRouteEndpoint": "${BINGO_PLAY_B_ROUTE_ENDPOINT}"
})
PY

endpoint_host() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  echo "${endpoint%:*}"
}

endpoint_port() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  echo "${endpoint##*:}"
}

wait_port() {
  local name="$1"
  local endpoint="$2"
  local host
  local port
  host="$(endpoint_host "${endpoint}")"
  port="$(endpoint_port "${endpoint}")"
  for _ in $(seq 1 100); do
    if (echo >"/dev/tcp/${host}/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name} at ${endpoint}" >&2
  return 1
}

wait_location_ready() {
  node - \
    "${BINGO_REDIS_ENDPOINT}" \
    "${BINGO_REDIS_KEY_PREFIX}location" \
    "${BINGO_API_A_ENDPOINT}" \
    "${BINGO_API_B_ENDPOINT}" \
    "${BINGO_PLAY_A_SPOT_ENDPOINT}" \
    "${BINGO_PLAY_B_SPOT_ENDPOINT}" <<'NODE'
const redisEndpoint = process.argv[2];
const keyPrefix = process.argv[3];
const expectedApiEndpoints = new Set(process.argv.slice(4, 6));
const expectedSpotEndpoints = new Set(process.argv.slice(6));
const framework = require('@zlink-systems/framework');
const { ZLinkRedisLocationStore } = require('@zlink-systems/framework-locations-redis');

const store = new ZLinkRedisLocationStore({
  url: `redis://${redisEndpoint}`,
  keyPrefix
});

async function main() {
  let lastPeers = [];
  let lastSpotPeers = [];
  let lastLeases = [];
  for (let attempt = 0; attempt < 100; attempt += 1) {
    lastPeers = await store.listPeers({
      autoConnectType: framework.ZLinkLocationAutoConnectType.ClientServer,
      meshName: 'bingo.api',
      role: framework.ZLinkLocationRole.Router
    });
    lastSpotPeers = await store.listPeers({
      autoConnectType: framework.ZLinkLocationAutoConnectType.SpotMesh,
      meshName: 'bingo.room',
      role: framework.ZLinkLocationRole.Spot
    });
    lastLeases = (await store.listOwnerLeases()).leases;
    if ([...expectedApiEndpoints].every((endpoint) =>
      lastPeers.some((peer) => peer.endpoint === endpoint && lastLeases.some((lease) => lease.ownerId === peer.ownerId))) &&
      [...expectedSpotEndpoints].every((endpoint) =>
        lastSpotPeers.some((peer) => peer.endpoint === endpoint && lastLeases.some((lease) => lease.ownerId === peer.ownerId)))) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  console.error('Timed out waiting for Bingo location readiness.');
  console.error(JSON.stringify({ peers: lastPeers, spotPeers: lastSpotPeers, leases: lastLeases }, (_key, value) =>
    typeof value === 'bigint' ? value.toString() : value, 2));
  process.exitCode = 1;
}

main()
  .finally(() => store.dispose())
  .catch((error) => {
    console.error(error);
    process.exitCode = 1;
  });
NODE
}

start_server() {
  local name="$1"
  local entry="$2"
  local config="$3"
  ZLINK_SAMPLE_CONFIG="${config}" node "${SCRIPT_DIR}/${entry}" >"${LOG_DIR}/${name}.log" 2>&1 &
  PIDS+=("$!")
}

(cd "${SCRIPT_DIR}" && npm run build >/dev/null)

wait_port redis "tcp://${BINGO_REDIS_ENDPOINT}"

start_server api-a dist/Server/Api/main.js "${API_A_CONFIG}"
wait_port api-a "${BINGO_API_A_ENDPOINT}"

start_server api-b dist/Server/Api/main.js "${API_B_CONFIG}"
wait_port api-b "${BINGO_API_B_ENDPOINT}"

start_server play-a dist/Server/Play/main.js "${PLAY_A_CONFIG}"
wait_port play-a-route "${BINGO_PLAY_A_ROUTE_ENDPOINT}"
wait_port play-a-spot "${BINGO_PLAY_A_SPOT_ENDPOINT}"
wait_port play-a-spot-pubsub "${BINGO_PLAY_A_SPOT_PUBSUB_ENDPOINT}"

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

ZLINK_SAMPLE_CONFIG="${CLIENT_CONFIG}" node "${SCRIPT_DIR}/dist/Client/main.js" >"${LOG_DIR}/client.log" 2>&1
grep -q "stream-inbound sample=Bingo" "${LOG_DIR}/client.log"
grep -Eq "stream-inbound sample=Bingo .* seq=[0-9]" "${LOG_DIR}/client.log"
grep -Eq "stream-inbound sample=Bingo .* name=.*Notify" "${LOG_DIR}/client.log"
grep -q "client=observer" "${LOG_DIR}/client.log"
grep -q "name=BingoRewardAnnouncedNotify" "${LOG_DIR}/client.log"
grep -Rq "message flow" "${BINGO_LOG_DIR}"
echo "bingo=completed"
echo "PASS Bingo.Ts"
