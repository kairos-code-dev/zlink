#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"
npm run build >/dev/null

RUN_DIR="${SHOPPINGMALL_RUN_DIR:-$(mktemp -d)}"
LOG_DIR="${RUN_DIR}/logs"
WORK_DIR="${RUN_DIR}/work"
export SHOPPINGMALL_WORK_DIR="${WORK_DIR}"
mkdir -p "${LOG_DIR}" "${WORK_DIR}"
PIDS=()

cleanup() {
  for ((i=${#PIDS[@]}-1; i>=0; i--)); do
    local pid="${PIDS[$i]}"
    kill -INT "${pid}" >/dev/null 2>&1 || true
  done
  for _ in $(seq 1 20); do
    local any_alive=0
    for pid in "${PIDS[@]}"; do
      if kill -0 "${pid}" >/dev/null 2>&1; then
        any_alive=1
        break
      fi
    done
    if [[ "${any_alive}" == "0" ]]; then
      break
    fi
    sleep 0.1
  done
  for pid in "${PIDS[@]}"; do
    if kill -0 "${pid}" >/dev/null 2>&1; then
      kill -9 "${pid}" >/dev/null 2>&1 || true
    fi
    wait "${pid}" 2>/dev/null || true
  done
  if [[ "${SHOPPINGMALL_KEEP_RUN_DIR:-}" == "1" ]]; then
    echo "runDir=${RUN_DIR}"
  else
    [[ -n "${SHOPPINGMALL_RUN_DIR:-}" ]] || rm -rf "${RUN_DIR}"
  fi
}
trap cleanup EXIT

PORT_OUTPUT="$(python3 - <<'PY' 2>/dev/null || true
import random
import socket

sockets = []
chosen = set()
try:
    while len(sockets) < 2:
        port = random.randint(41000, 60999)
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
if [[ -z "${PORT_OUTPUT}" ]]; then
  PORT_OUTPUT="$(python3 - <<'PY'
import random
base = random.randint(41000, 60000)
print(f"{base} {base + 1}")
PY
)"
fi
read -r -a PORTS <<<"${PORT_OUTPUT}"

export SHOPPINGMALL_API_A_HTTP="http://127.0.0.1:${PORTS[0]}"
export SHOPPINGMALL_API_B_HTTP="http://127.0.0.1:${PORTS[1]}"
export SHOPPINGMALL_WORKFLOW_A_ENDPOINT="tcp://127.0.0.1:${PORTS[0]}"
export SHOPPINGMALL_WORKFLOW_B_ENDPOINT="tcp://127.0.0.1:${PORTS[1]}"

wait_http() {
  local name="$1"
  local endpoint="$2"
  for _ in $(seq 1 300); do
    if curl -fsS "${endpoint}/health" >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name} at ${endpoint}" >&2
  return 1
}

wait_tcp_endpoint() {
  local name="$1"
  local endpoint="$2"
  wait_http "${name}" "${endpoint/tcp:\/\//http://}"
}

start_role() {
  local name="$1"
  stdbuf -oL -eL node "${SCRIPT_DIR}/dist/Server/main.js" --role "${name}" >"${LOG_DIR}/${name}.log" 2>&1 &
  PIDS+=("$!")
}

start_role api-a
wait_http api-a "${SHOPPINGMALL_API_A_HTTP}"
start_role api-b
wait_http api-b "${SHOPPINGMALL_API_B_HTTP}"

stdbuf -oL -eL node "${SCRIPT_DIR}/dist/Client/main.js" >"${LOG_DIR}/client.log" 2>&1

grep -q "shoppingmall=completed" "${LOG_DIR}/client.log"
grep -q "shoppingmall-server-evidence=completed" "${LOG_DIR}/client.log"
grep -q "PASS ShoppingMall.Ts" "${LOG_DIR}/client.log"
cat "${LOG_DIR}/client.log"
