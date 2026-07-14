#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"
npm run build >/dev/null

RUN_DIR="${DELIVERYDISPATCH_RUN_DIR:-$(mktemp -d)}"
LOG_DIR="${RUN_DIR}/logs"
WORK_DIR="${RUN_DIR}/work"
export DELIVERYDISPATCH_LOG_DIR="${DELIVERYDISPATCH_LOG_DIR:-${LOG_DIR}/flow}"
export DELIVERYDISPATCH_WORK_DIR="${WORK_DIR}"
mkdir -p "${LOG_DIR}" "${WORK_DIR}" "${DELIVERYDISPATCH_LOG_DIR}"
rm -f "${DELIVERYDISPATCH_LOG_DIR}"/*.log
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
  local cleanup_status=0
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
    set +e
    wait "${pid}" 2>/dev/null
    local wait_status=$?
    set -e
    if [[ "${wait_status}" == "139" ]]; then
      printf 'DeliveryDispatch server process %s exited with SIGSEGV\n' "${pid}" >&2
      cleanup_status=139
    fi
  done
  if [[ -n "${REDIS_CONTAINER_ID}" ]]; then
    timeout -k 2s 10s docker rm -fv "${REDIS_CONTAINER_ID}" >/dev/null 2>&1 || true
  fi
  if [[ "${DELIVERYDISPATCH_KEEP_RUN_DIR:-}" == "1" ]]; then
    echo "runDir=${RUN_DIR}"
  else
    [[ -n "${DELIVERYDISPATCH_RUN_DIR:-}" ]] || rm -rf "${RUN_DIR}"
  fi
  if [[ "${exit_status}" != "0" ]]; then
    return "${exit_status}"
  fi
  if [[ "${cleanup_status}" != "0" ]]; then
    return "${cleanup_status}"
  fi
}
trap cleanup EXIT

read -r -a PORTS <<<"$(python3 - <<'PY'
import random
import socket

sockets = []
chosen = set()
try:
    while len(sockets) < 16:
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

export DELIVERYDISPATCH_API_HTTP="http://127.0.0.1:${PORTS[2]}"
export DELIVERYDISPATCH_CENTER_ROUTE="tcp://127.0.0.1:${PORTS[3]}"
export DELIVERYDISPATCH_COURIER_STREAM="ws://127.0.0.1:${PORTS[4]}"
export DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTE="tcp://127.0.0.1:${PORTS[5]}"
export DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTE="tcp://127.0.0.1:${PORTS[6]}"
export DELIVERYDISPATCH_COURIER_ACTOR_NODE1_SPOT="tcp://127.0.0.1:${PORTS[7]}"
export DELIVERYDISPATCH_COURIER_ACTOR_NODE2_SPOT="tcp://127.0.0.1:${PORTS[8]}"
export DELIVERYDISPATCH_TRACKING_ROUTE="tcp://127.0.0.1:${PORTS[9]}"
export DELIVERYDISPATCH_TRACKING_SPOT="tcp://127.0.0.1:${PORTS[10]}"
export DELIVERYDISPATCH_SESSION_STREAM="ws://127.0.0.1:${PORTS[11]}"
export DELIVERYDISPATCH_SESSION_SPOT_ROUTER="tcp://127.0.0.1:${PORTS[12]}"
export DELIVERYDISPATCH_COURIER_SESSION_SPOT="tcp://127.0.0.1:${PORTS[13]}"
export DELIVERYDISPATCH_REDIS_KEY_PREFIX="${DELIVERYDISPATCH_REDIS_KEY_PREFIX:-deliverydispatch:node:${RANDOM}:$$:}"

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

wait_port() {
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
  shift
  node "${SCRIPT_DIR}/dist/Server/main.js" --role "${name}" "$@" >"${LOG_DIR}/${name}.log" 2>&1 &
  PIDS+=("$!")
}

start_redis_container "zlink-redis-node-deliverydispatch-${RANDOM}-$$" --label "systems.zlink.sample=deliverydispatch-ts" -p "127.0.0.1::6379" "${ZLINK_REDIS_IMAGE:-redis:7.2-alpine}"
export DELIVERYDISPATCH_REDIS_ENDPOINT="$(redis_container_endpoint "${REDIS_CONTAINER_ID}")"
wait_port redis "tcp://${DELIVERYDISPATCH_REDIS_ENDPOINT}"

start_role tracking
wait_port tracking-route "${DELIVERYDISPATCH_TRACKING_ROUTE}"
wait_port tracking-customer-spot "${DELIVERYDISPATCH_TRACKING_SPOT}"

start_role customer-gateway
wait_port session-stream "${DELIVERYDISPATCH_SESSION_STREAM}"

start_role courier-session
wait_port courier-session-stream "${DELIVERYDISPATCH_COURIER_STREAM}"
wait_port courier-session-spot "${DELIVERYDISPATCH_COURIER_SESSION_SPOT}"

start_role courier-spot-node1
wait_port courier-actor-node1 "${DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTE}"
wait_port courier-actor-node1-spot "${DELIVERYDISPATCH_COURIER_ACTOR_NODE1_SPOT}"

start_role courier-spot-node2
wait_port courier-actor-node2 "${DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTE}"
wait_port courier-actor-node2-spot "${DELIVERYDISPATCH_COURIER_ACTOR_NODE2_SPOT}"

start_role dispatch
wait_port dispatch-center "${DELIVERYDISPATCH_CENTER_ROUTE}"
wait_http dispatch-api "${DELIVERYDISPATCH_API_HTTP}"

node "${SCRIPT_DIR}/dist/Server/main.js" --role probe --timeout-ms 10000
node "${SCRIPT_DIR}/../../scripts/browser-e2e/run-sample.mjs" DeliveryDispatch.Ts

grep -q "deliverydispatch tracking: status" "${LOG_DIR}/tracking.log"
grep -q "deliverydispatch session: bound customer" "${LOG_DIR}/customer-gateway.log"
grep -q "deliverydispatch session: found existing customer=customer-1" "${LOG_DIR}/customer-gateway.log"
grep -q "deliverydispatch courier-session: found existing courier=courier-a" "${LOG_DIR}/courier-session.log"
grep -q "deliverydispatch courier-session: found existing courier=courier-b" "${LOG_DIR}/courier-session.log"
grep -Rq "message flow" "${DELIVERYDISPATCH_LOG_DIR}"
if grep -Rq "phase=error" "${DELIVERYDISPATCH_LOG_DIR}"; then
  echo "DeliveryDispatch message flow contains phase=error" >&2
  exit 1
fi
