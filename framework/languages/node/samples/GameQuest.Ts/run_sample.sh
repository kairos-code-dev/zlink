#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"
npm run build >/dev/null

RUN_DIR="$(mktemp -d)"
export GAMEQUEST_LOG_DIR="${GAMEQUEST_LOG_DIR:-${SCRIPT_DIR}/logs}"
export GAMEQUEST_STORE_DIR="${RUN_DIR}/store"
mkdir -p "${GAMEQUEST_LOG_DIR}"
rm -f "${GAMEQUEST_LOG_DIR}"/*.log
PIDS=()

cleanup() {
  local status=$?
  if [[ "${status}" != "0" ]]; then
    for log in "${RUN_DIR}"/*.log; do
      if [[ -f "${log}" ]]; then
        echo "===== ${log} =====" >&2
        tail -n 80 "${log}" >&2 || true
      fi
    done
  fi
  for pid in "${PIDS[@]}"; do
    kill "${pid}" >/dev/null 2>&1 || true
  done
  for pid in "${PIDS[@]}"; do
    wait "${pid}" 2>/dev/null || true
  done
  rm -rf "${RUN_DIR}"
}
trap cleanup EXIT

read -r GAMEQUEST_REGISTRY_PUB_ENDPOINT GAMEQUEST_REGISTRY_ROUTER_ENDPOINT GAMEQUEST_API_A_PORT GAMEQUEST_API_B_PORT GAMEQUEST_API_A_STREAM GAMEQUEST_API_B_STREAM GAMEQUEST_MISSION_A_ENDPOINT GAMEQUEST_MISSION_B_ENDPOINT <<<"$(python3 - <<'PY'
import socket
sockets = []
for _ in range(8):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    sockets.append(sock)
values = []
for index, sock in enumerate(sockets):
    port = sock.getsockname()[1]
    values.append(str(port) if index in (2, 3) else f"tcp://127.0.0.1:{port}")
print(" ".join(values))
for sock in sockets:
    sock.close()
PY
)"
export GAMEQUEST_REGISTRY_PUB_ENDPOINT
export GAMEQUEST_REGISTRY_ROUTER_ENDPOINT
export GAMEQUEST_API_A_HTTP="http://127.0.0.1:${GAMEQUEST_API_A_PORT}"
export GAMEQUEST_API_B_HTTP="http://127.0.0.1:${GAMEQUEST_API_B_PORT}"
export GAMEQUEST_API_A_STREAM
export GAMEQUEST_API_B_STREAM
export GAMEQUEST_MISSION_A_ENDPOINT
export GAMEQUEST_MISSION_B_ENDPOINT
export ZLINK_SAMPLE_CONFIG="${RUN_DIR}/sample.env"

wait_tcp_endpoint() {
  local endpoint="$1"
  local name="$2"
  local endpoint_host="${endpoint#tcp://}"
  endpoint_host="${endpoint_host%:*}"
  local endpoint_port="${endpoint##*:}"
  for _ in $(seq 1 100); do
    if (echo >"/dev/tcp/${endpoint_host}/${endpoint_port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name}" >&2
  return 1
}

wait_http() {
  local url="$1"
  for _ in $(seq 1 100); do
    if node -e "fetch('${url}/health').then(r => process.exit(r.ok ? 0 : 1)).catch(() => process.exit(1))" >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${url}" >&2
  return 1
}

wait_discovery_ready() {
  node - "${GAMEQUEST_REGISTRY_ROUTER_ENDPOINT}" <<'NODE'
const registryEndpoint = process.argv[2];
const zlink = require('@zlink-systems/zlink');
const pause = new Int32Array(new SharedArrayBuffer(4));
const context = zlink.createContext();
const client = zlink.createRegistryQueryClient(context);

try {
  client.connect(registryEndpoint);
  for (let attempt = 0; attempt < 100; attempt += 1) {
    const ready = client
      .topology()
      .filter((entry) =>
        entry.channelName === 'gamequest.quest.mission.route' &&
        entry.state === 3 &&
        typeof entry.endpoint === 'string' &&
        entry.endpoint.length > 0);
    if (ready.length >= 2) {
      process.exit(0);
    }
    Atomics.wait(pause, 0, 0, 100);
  }
  console.error('Timed out waiting for GameQuest discovery readiness.');
  process.exit(1);
} finally {
  client.close();
  context.close();
}
NODE
}

start_role() {
  local role="$1"
  node "${SCRIPT_DIR}/dist/Server/main.js" --role "${role}" >"${RUN_DIR}/${role}.log" 2>&1 &
  PIDS+=("$!")
}

run_client() {
  for attempt in $(seq 1 10); do
    if node "${SCRIPT_DIR}/dist/Client/main.js"; then
      return 0
    fi
    if [[ "${attempt}" == "10" ]]; then
      return 1
    fi
    sleep 0.2
  done
}

start_role registry
sleep 0.5
start_role mission-a
start_role mission-b
wait_tcp_endpoint "${GAMEQUEST_MISSION_A_ENDPOINT}" mission-a
wait_tcp_endpoint "${GAMEQUEST_MISSION_B_ENDPOINT}" mission-b
wait_discovery_ready
start_role api-a
start_role api-b
wait_tcp_endpoint "${GAMEQUEST_API_A_STREAM}" api-a-stream
wait_tcp_endpoint "${GAMEQUEST_API_B_STREAM}" api-b-stream
wait_http "${GAMEQUEST_API_A_HTTP}"
wait_http "${GAMEQUEST_API_B_HTTP}"
sleep 1
run_client
grep -q "label=api-a" "${GAMEQUEST_LOG_DIR}/flow-api-a.log"
grep -q "label=api-b" "${GAMEQUEST_LOG_DIR}/flow-api-b.log"
grep -q "label=mission-a" "${GAMEQUEST_LOG_DIR}/flow-mission-a.log"
grep -q "label=mission-b" "${GAMEQUEST_LOG_DIR}/flow-mission-b.log"
