#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_DIR="$(mktemp -d)"
LOG_DIR="${RUN_DIR}/logs"
mkdir -p "${LOG_DIR}"

PIDS=()

cleanup() {
  local status="$?"
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
    while len(sockets) < 6:
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

export BINGO_REGISTRY_PUB_ENDPOINT="${BINGO_REGISTRY_PUB_ENDPOINT:-tcp://127.0.0.1:${PORTS[0]}}"
export BINGO_REGISTRY_ROUTER_ENDPOINT="${BINGO_REGISTRY_ROUTER_ENDPOINT:-tcp://127.0.0.1:${PORTS[1]}}"
export BINGO_SESSION_ENDPOINT="${BINGO_SESSION_ENDPOINT:-tcp://127.0.0.1:${PORTS[2]}}"
export BINGO_PLAY_ENDPOINT="${BINGO_PLAY_ENDPOINT:-tcp://127.0.0.1:${PORTS[3]}}"
export BINGO_NOTIFICATION_ENDPOINT="${BINGO_NOTIFICATION_ENDPOINT:-tcp://127.0.0.1:${PORTS[4]}}"
export BINGO_API_ENDPOINT="${BINGO_API_ENDPOINT:-tcp://127.0.0.1:${PORTS[5]}}"
export ZLINK_SAMPLE_CONFIG="${RUN_DIR}/sample.config.json"

python3 - "${ZLINK_SAMPLE_CONFIG}" <<PY
import json
import sys

with open(sys.argv[1], "w", encoding="utf-8") as output:
    json.dump({
        "sample": {
            "registryPubEndpoint": "${BINGO_REGISTRY_PUB_ENDPOINT}",
            "registryRouterEndpoint": "${BINGO_REGISTRY_ROUTER_ENDPOINT}",
            "sessionEndpoint": "${BINGO_SESSION_ENDPOINT}",
            "playEndpoint": "${BINGO_PLAY_ENDPOINT}",
            "notificationEndpoint": "${BINGO_NOTIFICATION_ENDPOINT}",
            "apiEndpoint": "${BINGO_API_ENDPOINT}"
        }
    }, output, indent=2)
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

wait_discovery_ready() {
  node - "${BINGO_REGISTRY_ROUTER_ENDPOINT}" <<'NODE'
const registryEndpoint = process.argv[2];
const zlink = require('@zlink-systems/zlink');
const required = new Set(['bingo.api', 'bingo.play', 'bingo.notifications']);
const pause = new Int32Array(new SharedArrayBuffer(4));
const context = zlink.createContext();
const client = zlink.createRegistryQueryClient(context);

try {
  client.connect(registryEndpoint);
  for (let attempt = 0; attempt < 100; attempt += 1) {
    const ready = new Set(client
      .topology()
      .filter((entry) =>
        required.has(entry.channelName) &&
        entry.state === 3 &&
        typeof entry.endpoint === 'string' &&
        entry.endpoint.length > 0)
      .map((entry) => entry.channelName));
    if ([...required].every((channelName) => ready.has(channelName))) {
      process.exit(0);
    }
    Atomics.wait(pause, 0, 0, 100);
  }
  console.error('Timed out waiting for registry discovery readiness.');
  process.exit(1);
} finally {
  client.close();
  context.close();
}
NODE
}

start_server() {
  local name="$1"
  local entry="$2"
  node "${SCRIPT_DIR}/${entry}" >"${LOG_DIR}/${name}.log" 2>&1 &
  PIDS+=("$!")
}

(cd "${SCRIPT_DIR}" && npm run build >/dev/null)

start_server registry dist/Server/Registry/main.js
wait_port registry-pub "${BINGO_REGISTRY_PUB_ENDPOINT}"
wait_port registry-router "${BINGO_REGISTRY_ROUTER_ENDPOINT}"

start_server play dist/Server/Play/main.js
wait_port play "${BINGO_PLAY_ENDPOINT}"
wait_port notifications "${BINGO_NOTIFICATION_ENDPOINT}"

start_server api dist/Server/Api/main.js
wait_port api "${BINGO_API_ENDPOINT}"

start_server session dist/Server/Session/main.js
wait_port session "${BINGO_SESSION_ENDPOINT}"
wait_discovery_ready

node "${SCRIPT_DIR}/dist/Client/main.js" >"${LOG_DIR}/client.log" 2>&1
grep -q "stream-inbound sample=Bingo" "${LOG_DIR}/client.log"
grep -Eq "stream-inbound sample=Bingo .* seq=[0-9]" "${LOG_DIR}/client.log"
grep -Eq "stream-inbound sample=Bingo .* name=.*Notify" "${LOG_DIR}/client.log"
