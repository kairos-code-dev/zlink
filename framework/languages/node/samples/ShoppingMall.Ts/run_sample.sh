#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"
npm run build >/dev/null

RUN_DIR="$(mktemp -d)"
export SHOPPINGMALL_LOG_DIR="${SHOPPINGMALL_LOG_DIR:-${SCRIPT_DIR}/logs}"
mkdir -p "${SHOPPINGMALL_LOG_DIR}"
rm -f "${SHOPPINGMALL_LOG_DIR}"/*.log
PIDS=()

cleanup() {
  for pid in "${PIDS[@]}"; do
    kill "${pid}" >/dev/null 2>&1 || true
  done
  for pid in "${PIDS[@]}"; do
    wait "${pid}" 2>/dev/null || true
  done
  rm -rf "${RUN_DIR}"
}
trap cleanup EXIT

read -r SHOPPINGMALL_REGISTRY_PUB_ENDPOINT SHOPPINGMALL_REGISTRY_ROUTER_ENDPOINT SHOPPINGMALL_WORKFLOW_ENDPOINT <<<"$(python3 - <<'PY'
import socket
sockets = []
for _ in range(3):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    sockets.append(sock)
print(" ".join(f"tcp://127.0.0.1:{sock.getsockname()[1]}" for sock in sockets))
for sock in sockets:
    sock.close()
PY
)"
export SHOPPINGMALL_REGISTRY_PUB_ENDPOINT
export SHOPPINGMALL_REGISTRY_ROUTER_ENDPOINT
export SHOPPINGMALL_WORKFLOW_ENDPOINT
export ZLINK_SAMPLE_CONFIG="${RUN_DIR}/sample.env"

endpoint_host="${SHOPPINGMALL_WORKFLOW_ENDPOINT#tcp://}"
endpoint_host="${endpoint_host%:*}"
endpoint_port="${SHOPPINGMALL_WORKFLOW_ENDPOINT##*:}"

wait_port() {
  for _ in $(seq 1 100); do
    if (echo >"/dev/tcp/${endpoint_host}/${endpoint_port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ShoppingMall server" >&2
  return 1
}

wait_discovery_ready() {
  node - "${SHOPPINGMALL_REGISTRY_ROUTER_ENDPOINT}" <<'NODE'
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
      .some((entry) =>
        entry.channelName === 'shoppingmall.workflow' &&
        entry.state === 3 &&
        typeof entry.endpoint === 'string' &&
        entry.endpoint.length > 0);
    if (ready) {
      process.exit(0);
    }
    Atomics.wait(pause, 0, 0, 100);
  }
  console.error('Timed out waiting for ShoppingMall discovery readiness.');
  process.exit(1);
} finally {
  client.close();
  context.close();
}
NODE
}

start_server() {
  node "${SCRIPT_DIR}/dist/Server/main.js" >"${RUN_DIR}/server.log" 2>&1 &
  PIDS+=("$!")
}

start_server
wait_port
wait_discovery_ready
sleep 1
node "${SCRIPT_DIR}/dist/Client/main.js"
grep -Rq "message flow" "${SHOPPINGMALL_LOG_DIR}"
