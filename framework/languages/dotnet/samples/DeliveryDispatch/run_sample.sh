#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/../redis-common.sh"
RUN_DIR="${SAMPLE_RUN_DIR:-$(mktemp -d)}"
LOG_DIR="${RUN_DIR}/logs"
WORK_DIR="${RUN_DIR}/work"
export DELIVERYDISPATCH_LOG_DIR="${DELIVERYDISPATCH_LOG_DIR:-${SCRIPT_DIR}/logs}"
mkdir -p "${LOG_DIR}" "${WORK_DIR}" "${DELIVERYDISPATCH_LOG_DIR}"
rm -f "${DELIVERYDISPATCH_LOG_DIR}"/*.log

PIDS=()
REDIS_CONTAINER=""

cleanup() {
  for ((i=${#PIDS[@]}-1; i>=0; i--)); do
    local pid="${PIDS[$i]}"
    if kill -0 "${pid}" 2>/dev/null; then
      kill -INT "${pid}" 2>/dev/null || true
    fi
  done
  for _ in $(seq 1 20); do
    local any_alive=0
    for pid in "${PIDS[@]}"; do
      if kill -0 "${pid}" 2>/dev/null; then
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
    if kill -0 "${pid}" 2>/dev/null; then
      kill -9 "${pid}" 2>/dev/null || true
    fi
    wait "${pid}" 2>/dev/null || true
  done
  if [[ -n "${REDIS_CONTAINER}" ]]; then
    docker rm -f "${REDIS_CONTAINER}" >/dev/null 2>&1 || true
  fi
  if [[ "${DELIVERYDISPATCH_KEEP_RUN_DIR:-}" != "1" ]]; then
    [[ -z "${SAMPLE_RUN_DIR:-}" ]] && rm -rf "${RUN_DIR}" || true
  else
    echo "runDir=${RUN_DIR}"
  fi
}
trap cleanup EXIT

if [[ -n "${DELIVERYDISPATCH_BASE_PORT:-}" ]]; then
  PORTS=()
  for offset in $(seq 1 19); do
    PORTS+=("$((DELIVERYDISPATCH_BASE_PORT + offset))")
  done
else
  read -r -a PORTS <<<"$(python3 - <<'PY'
import random
import socket

sockets = []
chosen = set()
try:
    while len(sockets) < 19:
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
fi

export DELIVERYDISPATCH_REDIS_KEY_PREFIX="${DELIVERYDISPATCH_REDIS_KEY_PREFIX:-deliverydispatch:dotnet:${RANDOM}:$$:}"
export DELIVERYDISPATCH_DISPATCH_HTTP="http://127.0.0.1:${PORTS[2]}"
export DELIVERYDISPATCH_DISPATCH_CHANNEL="tcp://127.0.0.1:${PORTS[3]}"
export DELIVERYDISPATCH_TRACKING_CHANNEL="tcp://127.0.0.1:${PORTS[5]}"
export DELIVERYDISPATCH_TRACKING_SPOT_ROUTER="tcp://127.0.0.1:${PORTS[6]}"
export DELIVERYDISPATCH_TRACKING_SPOT="tcp://127.0.0.1:${PORTS[7]}"
export DELIVERYDISPATCH_CUSTOMER_STREAM="tcp://127.0.0.1:${PORTS[8]}"
export DELIVERYDISPATCH_CUSTOMER_SPOT_ROUTER="tcp://127.0.0.1:${PORTS[9]}"
export DELIVERYDISPATCH_CUSTOMER_SPOT="tcp://127.0.0.1:${PORTS[10]}"
export DELIVERYDISPATCH_COURIER_STREAM="tcp://127.0.0.1:${PORTS[11]}"
export DELIVERYDISPATCH_COURIER_SESSION_SPOT_ROUTER="tcp://127.0.0.1:${PORTS[12]}"
export DELIVERYDISPATCH_COURIER_SESSION_SPOT="tcp://127.0.0.1:${PORTS[13]}"
export DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTER="tcp://127.0.0.1:${PORTS[14]}"
export DELIVERYDISPATCH_COURIER_ACTOR_NODE1="tcp://127.0.0.1:${PORTS[15]}"
export DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTER="tcp://127.0.0.1:${PORTS[17]}"
export DELIVERYDISPATCH_COURIER_ACTOR_NODE2="tcp://127.0.0.1:${PORTS[18]}"
export DELIVERYDISPATCH_WORK_DIR="${WORK_DIR}"

endpoint_host() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#http://}"
  endpoint="${endpoint#ws://}"
  echo "${endpoint%:*}"
}

endpoint_port() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#http://}"
  endpoint="${endpoint#ws://}"
  echo "${endpoint##*:}"
}

wait_port() {
  local name="$1"
  local endpoint="$2"
  local host
  local port
  host="$(endpoint_host "${endpoint}")"
  port="$(endpoint_port "${endpoint}")"
  for _ in $(seq 1 120); do
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
  for _ in $(seq 1 120); do
    if curl -fsS "${endpoint}/health" >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${name} at ${endpoint}" >&2
  return 1
}

start_server() {
  local name="$1"
  local project="$2"
  shift 2
  local project_dir
  local project_name
  local assembly
  project_dir="$(cd "$(dirname "${project}")" && pwd)"
  project_name="$(basename "${project}" .csproj)"
  assembly="${project_dir}/bin/Debug/net8.0/${project_name}.dll"
  dotnet "${assembly}" "$@" >"${LOG_DIR}/${name}.log" 2>&1 &
  PIDS+=("$!")
}

wait_log() {
  local pattern="$1"
  local file="$2"
  for _ in $(seq 1 80); do
    if grep -Eq "${pattern}" "${file}"; then
      return 0
    fi
    sleep 0.2
  done
  echo "Timed out waiting for '${pattern}' in ${file}" >&2
  return 1
}

if [[ -z "${DELIVERYDISPATCH_REDIS_ENDPOINT:-}" ]]; then
  if ! command -v docker >/dev/null 2>&1; then
    echo "Docker is required to run the DeliveryDispatch sample when DELIVERYDISPATCH_REDIS_ENDPOINT is not set." >&2
    exit 1
  fi
  zlink_redis_start_scoped_assign REDIS_CONTAINER DELIVERYDISPATCH_REDIS_ENDPOINT "deliverydispatch-dotnet-redis" redis:7.2-alpine
  export DELIVERYDISPATCH_REDIS_ENDPOINT
fi
wait_port redis "tcp://${DELIVERYDISPATCH_REDIS_ENDPOINT}"

dotnet build "${SCRIPT_DIR}/DeliveryDispatch.sln" --maxcpucount:1

start_server tracking "${SCRIPT_DIR}/Server/Tracking/DeliveryDispatch.Server.Tracking.csproj"
wait_port tracking-channel "${DELIVERYDISPATCH_TRACKING_CHANNEL}"
wait_port tracking-spot-router "${DELIVERYDISPATCH_TRACKING_SPOT_ROUTER}"
wait_port tracking-spot "${DELIVERYDISPATCH_TRACKING_SPOT}"

start_server customer-gateway "${SCRIPT_DIR}/Server/CustomerGateway/DeliveryDispatch.Server.CustomerGateway.csproj"
wait_port customer-stream "${DELIVERYDISPATCH_CUSTOMER_STREAM}"
wait_port customer-spot-router "${DELIVERYDISPATCH_CUSTOMER_SPOT_ROUTER}"

start_server courier-actor-node1 "${SCRIPT_DIR}/Server/CourierActorNode/DeliveryDispatch.Server.CourierActorNode.csproj" --node node1
wait_port courier-actor-node1-router "${DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTER}"

start_server courier-actor-node2 "${SCRIPT_DIR}/Server/CourierActorNode/DeliveryDispatch.Server.CourierActorNode.csproj" --node node2
wait_port courier-actor-node2-router "${DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTER}"

start_server courier-session "${SCRIPT_DIR}/Server/CourierSession/DeliveryDispatch.Server.CourierSession.csproj"
wait_port courier-session-router "${DELIVERYDISPATCH_COURIER_SESSION_SPOT_ROUTER}"
wait_port courier-session-stream "${DELIVERYDISPATCH_COURIER_STREAM}"

start_server dispatch "${SCRIPT_DIR}/Server/Dispatch/DeliveryDispatch.Server.Dispatch.csproj"
wait_http dispatch "${DELIVERYDISPATCH_DISPATCH_HTTP}"

dotnet run --no-build --project "${SCRIPT_DIR}/Client/DeliveryDispatch.Client.csproj" -- \
  --api-url "${DELIVERYDISPATCH_DISPATCH_HTTP}" \
  --stream-endpoint "${DELIVERYDISPATCH_CUSTOMER_STREAM}" \
  --courier-stream-endpoint "${DELIVERYDISPATCH_COURIER_STREAM}" >"${LOG_DIR}/client.log" 2>&1

grep -q "deliverydispatch=completed" "${LOG_DIR}/client.log"
grep -q "topology=ready" "${LOG_DIR}/client.log"
grep -q "deliverydispatch-reassignment=completed" "${LOG_DIR}/client.log"
grep -q "deliverydispatch-server-evidence=completed" "${LOG_DIR}/client.log"
wait_log "deliverydispatch tracking: status" "${LOG_DIR}/tracking.log"
wait_log "deliverydispatch customer-session: bound customer" "${LOG_DIR}/customer-gateway.log"
wait_log "deliverydispatch customer-entry: pushed status" "${LOG_DIR}/customer-gateway.log"
wait_log "deliverydispatch courier-session: bound courier=courier-a" "${LOG_DIR}/courier-session.log"
wait_log "deliverydispatch courier-session: bound courier=courier-b" "${LOG_DIR}/courier-session.log"
grep -Rq "message flow" "${DELIVERYDISPATCH_LOG_DIR}"
echo "deliverydispatch-runner-evidence=completed"
