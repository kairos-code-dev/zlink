#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"
npm run build >/dev/null

RUN_DIR="${SUPPORTCHAT_RUN_DIR:-$(mktemp -d)}"
LOG_DIR="${RUN_DIR}/logs"
WORK_DIR="${RUN_DIR}/work"
export SUPPORTCHAT_LOG_DIR="${SUPPORTCHAT_LOG_DIR:-${SCRIPT_DIR}/logs}"
export SUPPORTCHAT_WORK_DIR="${WORK_DIR}"
export SUPPORTCHAT_STATE_FILE="${WORK_DIR}/supportchat-state.json"
mkdir -p "${LOG_DIR}" "${WORK_DIR}" "${SUPPORTCHAT_LOG_DIR}"
rm -f "${SUPPORTCHAT_LOG_DIR}"/*.log
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
  if [[ "${SUPPORTCHAT_KEEP_RUN_DIR:-}" == "1" ]]; then
    echo "runDir=${RUN_DIR}"
  else
    [[ -n "${SUPPORTCHAT_RUN_DIR:-}" ]] || rm -rf "${RUN_DIR}"
  fi
}
trap cleanup EXIT

PORT_OUTPUT="$(python3 - <<'PY' 2>/dev/null || true
import random
import socket

sockets = []
chosen = set()
try:
    while len(sockets) < 4:
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
print(" ".join(str(base + i) for i in range(4)))
PY
)"
fi
read -r -a PORTS <<<"${PORT_OUTPUT}"

export SUPPORTCHAT_API_HTTP="http://127.0.0.1:${PORTS[0]}"
export SUPPORTCHAT_API_CHANNEL_ENDPOINT="tcp://127.0.0.1:${PORTS[1]}"
export SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT="tcp://127.0.0.1:${PORTS[2]}"
export SUPPORTCHAT_STREAM_ENDPOINT="tcp://127.0.0.1:${PORTS[3]}"

wait_port() {
  local endpoint="$1"
  for _ in $(seq 1 300); do
    if curl -fsS "${endpoint}/health" >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${endpoint}" >&2
  return 1
}

start_server() {
  node "${SCRIPT_DIR}/dist/Server/Api/main.js" >"${LOG_DIR}/api.log" 2>&1 &
  PIDS+=("$!")
}

start_server
wait_port "${SUPPORTCHAT_API_HTTP}"

node "${SCRIPT_DIR}/dist/Server/Probe/main.js" >"${LOG_DIR}/probe.log" 2>&1
node "${SCRIPT_DIR}/dist/Client/main.js" >"${LOG_DIR}/client.log" 2>&1

grep -q "supportchat=completed" "${LOG_DIR}/client.log"
grep -q "supportchat-closed-typing-ignore=verified" "${LOG_DIR}/client.log"
grep -q "PASS SupportChat.Ts" "${LOG_DIR}/client.log"
grep -q "supportchat topology=ready" "${LOG_DIR}/probe.log"
cat "${LOG_DIR}/client.log"
