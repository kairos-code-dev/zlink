#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"
npm run build >/dev/null

RUN_DIR="${SUPPORTCHAT_RUN_DIR:-$(mktemp -d)}"
LOG_DIR="${RUN_DIR}/logs"
export SUPPORTCHAT_LOG_DIR="${SUPPORTCHAT_LOG_DIR:-${LOG_DIR}}"
mkdir -p "${LOG_DIR}" "${SUPPORTCHAT_LOG_DIR}"
PIDS=()
REDIS_CONTAINER_ID=""
source "${SCRIPT_DIR}/../../e2e/redis-container.sh"

print_failure_logs() {
  local file
  for file in "${LOG_DIR}"/*.log; do
    [[ -f "${file}" ]] || continue
    printf '===== %s =====\n' "${file}" >&2
    tail -n 80 "${file}" >&2
  done
}

cleanup() {
  local exit_status=$?
  if [[ "${exit_status}" != "0" ]]; then
    print_failure_logs
  fi
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
  if [[ -n "${REDIS_CONTAINER_ID}" ]]; then
    timeout -k 2s 10s docker rm -fv "${REDIS_CONTAINER_ID}" >/dev/null 2>&1 || true
  fi
  if [[ "${SUPPORTCHAT_KEEP_RUN_DIR:-}" == "1" ]]; then
    echo "runDir=${RUN_DIR}"
  else
    [[ -n "${SUPPORTCHAT_RUN_DIR:-}" ]] || rm -rf "${RUN_DIR}"
  fi
  return "${exit_status}"
}
trap cleanup EXIT

PORT_OUTPUT="$(python3 - <<'PY' 2>/dev/null || true
import random
import socket

sockets = []
chosen = set()
try:
    while len(sockets) < 5:
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
print(" ".join(str(base + i) for i in range(5)))
PY
)"
fi
read -r -a PORTS <<<"${PORT_OUTPUT}"

export SUPPORTCHAT_API_CHANNEL_ENDPOINT="tcp://127.0.0.1:${PORTS[0]}"
export SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT="tcp://127.0.0.1:${PORTS[1]}"
export SUPPORTCHAT_SUPPORT_SPOT_ENDPOINT="tcp://127.0.0.1:${PORTS[2]}"
export SUPPORTCHAT_SESSION_SPOT_ENDPOINT="tcp://127.0.0.1:${PORTS[3]}"
export SUPPORTCHAT_STREAM_ENDPOINT="ws://127.0.0.1:${PORTS[4]}"
export SUPPORTCHAT_REDIS_KEY_PREFIX="supportchat:node:${RANDOM}:$$:"

wait_tcp() {
  local name="$1"
  local endpoint="$2"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#ws://}"
  local host="${endpoint%:*}"
  local port="${endpoint##*:}"
  for _ in $(seq 1 300); do
    if timeout 1 bash -c ":</dev/tcp/${host}/${port}" >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name} at ${endpoint}" >&2
  return 1
}

wait_ready_log() {
  local name="$1"
  local pattern="$2"
  local file="${LOG_DIR}/${name}.log"
  for _ in $(seq 1 300); do
    if grep -q "${pattern}" "${file}" 2>/dev/null; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name} ready marker" >&2
  return 1
}

start_server() {
  local name="$1"
  local main="$2"
  node "${SCRIPT_DIR}/${main}" >"${LOG_DIR}/${name}.log" 2>&1 &
  PIDS+=("$!")
}

if ! command -v docker >/dev/null 2>&1; then
  echo "Docker is required to run the SupportChat sample because it provisions a dedicated Redis location store." >&2
  exit 1
fi

start_redis_container "zlink-redis-node-supportchat-${RANDOM}-$$" -p "127.0.0.1::6379" "${ZLINK_REDIS_IMAGE:-redis:7.2-alpine}"
export SUPPORTCHAT_REDIS_ENDPOINT="$(redis_container_endpoint "${REDIS_CONTAINER_ID}")"
wait_tcp redis "${SUPPORTCHAT_REDIS_ENDPOINT}"

start_server support "dist/Server/Support/main.js"
wait_ready_log support '"role":"support"'
wait_tcp support-channel "${SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT}"
wait_tcp support-spot "${SUPPORTCHAT_SUPPORT_SPOT_ENDPOINT}"
start_server api "dist/Server/Api/main.js"
wait_ready_log api '"role":"api"'
wait_tcp api-channel "${SUPPORTCHAT_API_CHANNEL_ENDPOINT}"
start_server session "dist/Server/Session/main.js"
wait_ready_log session '"role":"session"'
wait_tcp session-spot "${SUPPORTCHAT_SESSION_SPOT_ENDPOINT}"
wait_tcp session-stream "${SUPPORTCHAT_STREAM_ENDPOINT}"

node "${SCRIPT_DIR}/../../e2e/location-readiness.js" \
  --redis-endpoint "${SUPPORTCHAT_REDIS_ENDPOINT}" \
  --key-prefix "${SUPPORTCHAT_REDIS_KEY_PREFIX}location" \
  --peer client-server supportchat.api router "${SUPPORTCHAT_API_CHANNEL_ENDPOINT}" \
  --peer client-server supportchat.support router "${SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT}" \
  --peer spot-mesh supportchat-conversations spot "${SUPPORTCHAT_SUPPORT_SPOT_ENDPOINT}"
node "${SCRIPT_DIR}/../../scripts/browser-e2e/run-sample.mjs" SupportChat.Ts >"${LOG_DIR}/client.log" 2>&1

grep -q "supportchat=completed" "${LOG_DIR}/client.log"
grep -q "supportchat-closed-typing-ignore=verified" "${LOG_DIR}/client.log"
grep -q "PASS SupportChat.Ts" "${LOG_DIR}/client.log"
grep -q "stream-inbound sample=SupportChat" "${LOG_DIR}/client.log"
cat "${LOG_DIR}/client.log"
