#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"
npm run build >/dev/null

RUN_DIR="${GAMEQUEST_RUN_DIR:-$(mktemp -d)}"
LOG_DIR="${RUN_DIR}/logs"
WORK_DIR="${RUN_DIR}/work"
export GAMEQUEST_LOG_DIR="${GAMEQUEST_LOG_DIR:-${LOG_DIR}}"
export GAMEQUEST_WORK_DIR="${WORK_DIR}"
mkdir -p "${LOG_DIR}" "${WORK_DIR}" "${GAMEQUEST_LOG_DIR}"
rm -f "${GAMEQUEST_LOG_DIR}"/*.log
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
    while len(sockets) < 14:
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
export GAMEQUEST_API_A_STREAM="ws://127.0.0.1:${PORTS[2]}"
export GAMEQUEST_API_B_STREAM="ws://127.0.0.1:${PORTS[3]}"
export GAMEQUEST_API_A_ACTOR_SPOT="tcp://127.0.0.1:${PORTS[4]}"
export GAMEQUEST_API_B_ACTOR_SPOT="tcp://127.0.0.1:${PORTS[5]}"
export GAMEQUEST_MISSION_A_ROUTE="tcp://127.0.0.1:${PORTS[6]}"
export GAMEQUEST_MISSION_B_ROUTE="tcp://127.0.0.1:${PORTS[7]}"
export GAMEQUEST_MISSION_A_SPOT="tcp://127.0.0.1:${PORTS[8]}"
export GAMEQUEST_MISSION_B_SPOT="tcp://127.0.0.1:${PORTS[9]}"
export GAMEQUEST_MISSION_A_SPOT_ROUTER="tcp://127.0.0.1:${PORTS[10]}"
export GAMEQUEST_MISSION_B_SPOT_ROUTER="tcp://127.0.0.1:${PORTS[11]}"
export GAMEQUEST_MISSION_A_HTTP="http://127.0.0.1:${PORTS[12]}"
export GAMEQUEST_MISSION_B_HTTP="http://127.0.0.1:${PORTS[13]}"
export GAMEQUEST_REDIS_KEY_PREFIX="${GAMEQUEST_REDIS_KEY_PREFIX:-gamequest:node:${RANDOM}:$$:}"

endpoint_host() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#ws://}"
  endpoint="${endpoint#http://}"
  echo "${endpoint%:*}"
}

endpoint_port() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#ws://}"
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

start_redis_container "zlink-redis-node-gamequest-${RANDOM}-$$" -p "127.0.0.1::6379" "${ZLINK_REDIS_IMAGE:-redis:7.2-alpine}"
export GAMEQUEST_REDIS_ENDPOINT="$(redis_container_endpoint "${REDIS_CONTAINER_ID}")"
wait_tcp_endpoint redis "tcp://${GAMEQUEST_REDIS_ENDPOINT}"

start_role mission-a
wait_tcp_endpoint mission-a "${GAMEQUEST_MISSION_A_ROUTE}"
wait_tcp_endpoint mission-a-spot-router "${GAMEQUEST_MISSION_A_SPOT_ROUTER}"
wait_tcp_endpoint mission-a-spot "${GAMEQUEST_MISSION_A_SPOT}"
wait_http mission-a "${GAMEQUEST_MISSION_A_HTTP}"
start_role mission-b
wait_tcp_endpoint mission-b "${GAMEQUEST_MISSION_B_ROUTE}"
wait_tcp_endpoint mission-b-spot-router "${GAMEQUEST_MISSION_B_SPOT_ROUTER}"
wait_tcp_endpoint mission-b-spot "${GAMEQUEST_MISSION_B_SPOT}"
wait_http mission-b "${GAMEQUEST_MISSION_B_HTTP}"
start_role api-a
wait_tcp_endpoint api-a-stream "${GAMEQUEST_API_A_STREAM}"
wait_tcp_endpoint api-a-actor-spot "${GAMEQUEST_API_A_ACTOR_SPOT}"
wait_http api-a "${GAMEQUEST_API_A_HTTP}"
start_role api-b
wait_tcp_endpoint api-b-stream "${GAMEQUEST_API_B_STREAM}"
wait_tcp_endpoint api-b-actor-spot "${GAMEQUEST_API_B_ACTOR_SPOT}"
wait_http api-b "${GAMEQUEST_API_B_HTTP}"

node "${SCRIPT_DIR}/../../scripts/browser-e2e/run-sample.mjs" GameQuest.Ts

grep -q "gamequest api event routed" "${LOG_DIR}/api-a.log"
grep -Rq "packet=GameplayMsg" "${GAMEQUEST_LOG_DIR}"
grep -Rq "surface=spotActor.*packet=QuestCompletedNotify" "${GAMEQUEST_LOG_DIR}"
grep -q '"role":"mission-a"' "${LOG_DIR}/mission-a.log"
grep -q '"role":"mission-b"' "${LOG_DIR}/mission-b.log"
echo "gamequest-server-evidence=completed"
