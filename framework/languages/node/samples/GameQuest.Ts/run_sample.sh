#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"
npm run build >/dev/null

RUN_DIR="${GAMEQUEST_RUN_DIR:-$(mktemp -d)}"
LOG_DIR="${RUN_DIR}/logs"
WORK_DIR="${RUN_DIR}/work"
export GAMEQUEST_LOG_DIR="${GAMEQUEST_LOG_DIR:-${SCRIPT_DIR}/logs}"
export GAMEQUEST_WORK_DIR="${WORK_DIR}"
mkdir -p "${LOG_DIR}" "${WORK_DIR}" "${GAMEQUEST_LOG_DIR}"
rm -f "${GAMEQUEST_LOG_DIR}"/*.log
PIDS=()
REDIS_CONTAINER_ID=""

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
    docker rm -f "${REDIS_CONTAINER_ID}" >/dev/null 2>&1 || true
  fi
  if [[ "${GAMEQUEST_KEEP_RUN_DIR:-}" == "1" ]]; then
    echo "runDir=${RUN_DIR}"
  else
    [[ -n "${GAMEQUEST_RUN_DIR:-}" ]] || rm -rf "${RUN_DIR}"
  fi
}
trap cleanup EXIT

if ! PORT_OUTPUT="$(python3 - <<'PY'
import random
import socket

sockets = []
chosen = set()
try:
    while len(sockets) < 8:
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
)"; then
  echo "Unable to reserve loopback TCP ports for GameQuest.Ts; environment denied socket bind." >&2
  exit 1
fi
read -r -a PORTS <<<"${PORT_OUTPUT}"

export GAMEQUEST_API_A_HTTP="http://127.0.0.1:${PORTS[0]}"
export GAMEQUEST_API_B_HTTP="http://127.0.0.1:${PORTS[1]}"
export GAMEQUEST_API_A_STREAM="tcp://127.0.0.1:${PORTS[2]}"
export GAMEQUEST_API_B_STREAM="tcp://127.0.0.1:${PORTS[3]}"
export GAMEQUEST_MISSION_A_ROUTE="tcp://127.0.0.1:${PORTS[4]}"
export GAMEQUEST_MISSION_B_ROUTE="tcp://127.0.0.1:${PORTS[5]}"
export GAMEQUEST_MISSION_A_HTTP="http://127.0.0.1:${PORTS[6]}"
export GAMEQUEST_MISSION_B_HTTP="http://127.0.0.1:${PORTS[7]}"
export GAMEQUEST_REDIS_KEY_PREFIX="${GAMEQUEST_REDIS_KEY_PREFIX:-gamequest:node:${RANDOM}:$$:}"

endpoint_host() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#http://}"
  echo "${endpoint%:*}"
}

endpoint_port() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#http://}"
  echo "${endpoint##*:}"
}

wait_tcp_endpoint() {
  local name="$1"
  local endpoint="$2"
  local host
  local port
  host="$(endpoint_host "${endpoint}")"
  port="$(endpoint_port "${endpoint}")"
  for _ in $(seq 1 300); do
    if (echo >"/dev/tcp/${host}/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name} at ${endpoint}" >&2
  return 1
}

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

start_role() {
  local name="$1"
  node "${SCRIPT_DIR}/dist/Server/main.js" --role "${name}" >"${LOG_DIR}/${name}.log" 2>&1 &
  PIDS+=("$!")
}

REDIS_CONTAINER_ID="$(docker run -d --rm --name "gamequest-node-redis-${RANDOM}-$$" -p "127.0.0.1::6379" redis:7.2-alpine)"
export GAMEQUEST_REDIS_ENDPOINT="$(docker port "${REDIS_CONTAINER_ID}" 6379/tcp | sed -E 's/.*:([0-9]+)$/127.0.0.1:\1/')"
wait_tcp_endpoint redis "tcp://${GAMEQUEST_REDIS_ENDPOINT}"

start_role mission-a
wait_tcp_endpoint mission-a "${GAMEQUEST_MISSION_A_ROUTE}"
start_role mission-b
wait_tcp_endpoint mission-b "${GAMEQUEST_MISSION_B_ROUTE}"

start_role api-a
wait_tcp_endpoint api-a-stream "${GAMEQUEST_API_A_STREAM}"
wait_http api-a "${GAMEQUEST_API_A_HTTP}"
start_role api-b
wait_tcp_endpoint api-b-stream "${GAMEQUEST_API_B_STREAM}"
wait_http api-b "${GAMEQUEST_API_B_HTTP}"

node "${SCRIPT_DIR}/dist/Client/main.js"

grep -q "gamequest api event routed" "${LOG_DIR}/api-a.log"
grep -Rq "gamequest mission processed" "${LOG_DIR}"
grep -Rq "message flow" "${GAMEQUEST_LOG_DIR}"
echo "gamequest-server-evidence=completed"
