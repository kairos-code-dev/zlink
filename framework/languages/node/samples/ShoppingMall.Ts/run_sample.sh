#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"
npm run build >/dev/null

RUN_DIR="$(mktemp -d)"
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

read -r SHOPPINGMALL_WORKFLOW_ENDPOINT <<<"$(python3 - <<'PY'
import socket
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.bind(("127.0.0.1", 0))
print(f"tcp://127.0.0.1:{sock.getsockname()[1]}")
sock.close()
PY
)"
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

start_server() {
  node "${SCRIPT_DIR}/dist/Server/main.js" >"${RUN_DIR}/server.log" 2>&1 &
  PIDS+=("$!")
}

start_server
wait_port
node "${SCRIPT_DIR}/dist/Client/main.js"
