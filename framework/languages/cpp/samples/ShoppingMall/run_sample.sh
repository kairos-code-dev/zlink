#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
export SHOPPINGMALL_LOG_DIR="${SHOPPINGMALL_LOG_DIR:-${SCRIPT_DIR}/logs}"
BUILD_DIR="${ZLINK_CPP_BUILD_DIR:-$CPP_ROOT/build}"
BIN_DIR="$BUILD_DIR"
if [[ ! -x "$BIN_DIR/sample_cpp_framework_shoppingmall_client" && -x "$BIN_DIR/linux-ninja-debug/sample_cpp_framework_shoppingmall_client" ]]; then
  BIN_DIR="$BIN_DIR/linux-ninja-debug"
fi
PIDS=()
cleanup() {
  for pid in "${PIDS[@]}"; do
    kill "${pid}" >/dev/null 2>&1 || true
    wait "${pid}" 2>/dev/null || true
  done
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

"${BIN_DIR}/sample_cpp_framework_shoppingmall_server" &
PIDS+=("$!")

endpoint_port="${SHOPPINGMALL_WORKFLOW_ENDPOINT##*:}"
for _ in $(seq 1 100); do
  if (echo >"/dev/tcp/127.0.0.1/${endpoint_port}") >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done

sleep 1
"${BIN_DIR}/sample_cpp_framework_shoppingmall_client"
