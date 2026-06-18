#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${ZLINK_CPP_BUILD_DIR:-$CPP_ROOT/build}"
BIN_DIR="$BUILD_DIR"
if [[ ! -x "$BIN_DIR/sample_cpp_framework_gamequest_client" && -x "$BIN_DIR/linux-ninja-debug/sample_cpp_framework_gamequest_client" ]]; then
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

read -r GAMEQUEST_QUEST_ENDPOINT <<<"$(python3 - <<'PY'
import socket
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.bind(("127.0.0.1", 0))
print(f"tcp://127.0.0.1:{sock.getsockname()[1]}")
sock.close()
PY
)"
export GAMEQUEST_QUEST_ENDPOINT
endpoint_port="${GAMEQUEST_QUEST_ENDPOINT##*:}"

"${BIN_DIR}/sample_cpp_framework_gamequest_server" &
PIDS+=("$!")

for _ in $(seq 1 100); do
  if (echo >"/dev/tcp/127.0.0.1/${endpoint_port}") >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done

"${BIN_DIR}/sample_cpp_framework_gamequest_client"
