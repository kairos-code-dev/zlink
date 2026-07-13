#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"
npm run build >/dev/null

RUN_DIR="${SHOPPINGMALL_RUN_DIR:-$(mktemp -d)}"
LOG_DIR="${RUN_DIR}/logs"
WORK_DIR="${RUN_DIR}/work"
export SHOPPINGMALL_WORK_DIR="${WORK_DIR}"
export SHOPPINGMALL_LOG_DIR="${LOG_DIR}"
mkdir -p "${LOG_DIR}" "${WORK_DIR}"
PIDS=()
REDIS_CONTAINER_ID=""
source "${SCRIPT_DIR}/../../e2e/redis-container.sh"

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
  if [[ -n "${REDIS_CONTAINER_ID}" ]]; then
    timeout -k 2s 10s docker rm -fv "${REDIS_CONTAINER_ID}" >/dev/null 2>&1 || true
  fi
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
    while len(sockets) < 10:
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
print(" ".join(str(base + i) for i in range(10)))
PY
)"
fi
read -r -a PORTS <<<"${PORT_OUTPUT}"

export SHOPPINGMALL_API_A_HTTP="http://127.0.0.1:${PORTS[0]}"
export SHOPPINGMALL_API_B_HTTP="http://127.0.0.1:${PORTS[1]}"
export SHOPPINGMALL_WORKFLOW_A_HTTP="http://127.0.0.1:${PORTS[2]}"
export SHOPPINGMALL_WORKFLOW_B_HTTP="http://127.0.0.1:${PORTS[3]}"
export SHOPPINGMALL_WORKFLOW_A_CHANNEL_ENDPOINT="tcp://127.0.0.1:${PORTS[4]}"
export SHOPPINGMALL_WORKFLOW_B_CHANNEL_ENDPOINT="tcp://127.0.0.1:${PORTS[5]}"
export SHOPPINGMALL_WORKFLOW_A_SPOT_ENDPOINT="tcp://127.0.0.1:${PORTS[6]}"
export SHOPPINGMALL_WORKFLOW_B_SPOT_ENDPOINT="tcp://127.0.0.1:${PORTS[7]}"
export SHOPPINGMALL_WORKFLOW_A_SPOT_PUB_ENDPOINT="tcp://127.0.0.1:${PORTS[8]}"
export SHOPPINGMALL_WORKFLOW_B_SPOT_PUB_ENDPOINT="tcp://127.0.0.1:${PORTS[9]}"
export SHOPPINGMALL_REDIS_KEY_PREFIX="shoppingmall:node:${RANDOM}:$$:"

wait_http() {
  local name="$1"
  local endpoint="$2"
  for _ in $(seq 1 20); do
    if curl --connect-timeout 1 --max-time 6 -fsS "${endpoint}/health" >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name} at ${endpoint}" >&2
  curl --connect-timeout 1 --max-time 6 -sS "${endpoint}/health" >&2 || true
  echo >&2
  return 1
}

wait_tcp_endpoint() {
  local name="$1"
  local endpoint="$2"
  local host="${endpoint#tcp://}"
  host="${host%:*}"
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

start_role() {
  local name="$1"
  stdbuf -oL -eL node "${SCRIPT_DIR}/dist/Server/main.js" --role "${name}" >"${LOG_DIR}/${name}.log" 2>&1 &
  PIDS+=("$!")
}

if ! command -v docker >/dev/null 2>&1; then
  echo "Docker is required to run the ShoppingMall sample because it provisions a dedicated Redis location store." >&2
  exit 1
fi

start_redis_container "zlink-redis-node-sample-shoppingmall-${RANDOM}-$$" -p "127.0.0.1::6379" "${ZLINK_REDIS_IMAGE:-redis:7.2-alpine}"
export SHOPPINGMALL_REDIS_ENDPOINT="$(redis_container_endpoint "${REDIS_CONTAINER_ID}")"
wait_tcp_endpoint redis "tcp://${SHOPPINGMALL_REDIS_ENDPOINT}"

start_role workflow-a
start_role workflow-b
wait_http workflow-a "${SHOPPINGMALL_WORKFLOW_A_HTTP}"
wait_tcp_endpoint workflow-a-channel "${SHOPPINGMALL_WORKFLOW_A_CHANNEL_ENDPOINT}"
wait_tcp_endpoint workflow-a-spot "${SHOPPINGMALL_WORKFLOW_A_SPOT_ENDPOINT}"
wait_tcp_endpoint workflow-a-spot-pub "${SHOPPINGMALL_WORKFLOW_A_SPOT_PUB_ENDPOINT}"
wait_http workflow-b "${SHOPPINGMALL_WORKFLOW_B_HTTP}"
wait_tcp_endpoint workflow-b-channel "${SHOPPINGMALL_WORKFLOW_B_CHANNEL_ENDPOINT}"
wait_tcp_endpoint workflow-b-spot "${SHOPPINGMALL_WORKFLOW_B_SPOT_ENDPOINT}"
wait_tcp_endpoint workflow-b-spot-pub "${SHOPPINGMALL_WORKFLOW_B_SPOT_PUB_ENDPOINT}"
start_role api-a
wait_http api-a "${SHOPPINGMALL_API_A_HTTP}"
start_role api-b
wait_http api-b "${SHOPPINGMALL_API_B_HTTP}"

stdbuf -oL -eL node "${SCRIPT_DIR}/dist/Client/main.js" >"${LOG_DIR}/client.log" 2>&1

grep -q "shoppingmall=completed" "${LOG_DIR}/client.log"
grep -q "shoppingmall-server-evidence=completed" "${LOG_DIR}/client.log"
grep -q "PASS ShoppingMall.Ts" "${LOG_DIR}/client.log"
cat "${LOG_DIR}/client.log"
