#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"
npm run build >/dev/null

RUN_DIR="$(mktemp -d)"
export SHOPPINGMALL_LOG_DIR="${SHOPPINGMALL_LOG_DIR:-${SCRIPT_DIR}/logs}"
export SHOPPINGMALL_STORE_DIR="${RUN_DIR}/store"
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

read -r SHOPPINGMALL_REGISTRY_PUB_ENDPOINT SHOPPINGMALL_REGISTRY_ROUTER_ENDPOINT SHOPPINGMALL_API_A_PORT SHOPPINGMALL_API_B_PORT SHOPPINGMALL_WORKFLOW_A_ENDPOINT SHOPPINGMALL_WORKFLOW_B_ENDPOINT <<<"$(python3 - <<'PY'
import socket
sockets = []
for _ in range(6):
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
export SHOPPINGMALL_REGISTRY_PUB_ENDPOINT
export SHOPPINGMALL_REGISTRY_ROUTER_ENDPOINT
export SHOPPINGMALL_API_A_HTTP="http://127.0.0.1:${SHOPPINGMALL_API_A_PORT}"
export SHOPPINGMALL_API_B_HTTP="http://127.0.0.1:${SHOPPINGMALL_API_B_PORT}"
export SHOPPINGMALL_WORKFLOW_A_ENDPOINT
export SHOPPINGMALL_WORKFLOW_B_ENDPOINT
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
      .filter((entry) =>
        entry.channelName === 'shoppingmall.order.workflow.route' &&
        entry.state === 3 &&
        typeof entry.endpoint === 'string' &&
        entry.endpoint.length > 0);
    if (ready.length >= 2) {
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
start_role workflow-a
start_role workflow-b
wait_tcp_endpoint "${SHOPPINGMALL_WORKFLOW_A_ENDPOINT}" workflow-a
wait_tcp_endpoint "${SHOPPINGMALL_WORKFLOW_B_ENDPOINT}" workflow-b
wait_discovery_ready
start_role api-a
start_role api-b
wait_http "${SHOPPINGMALL_API_A_HTTP}"
wait_http "${SHOPPINGMALL_API_B_HTTP}"
sleep 1
run_client
grep -q "label=api-a" "${SHOPPINGMALL_LOG_DIR}/flow-api-a.log"
grep -q "label=api-b" "${SHOPPINGMALL_LOG_DIR}/flow-api-b.log"
grep -q "label=workflow-a" "${SHOPPINGMALL_LOG_DIR}/flow-workflow-a.log"
grep -q "label=workflow-b" "${SHOPPINGMALL_LOG_DIR}/flow-workflow-b.log"
