#!/usr/bin/env bash
set -euo pipefail
umask 077

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/../redis-common.sh"
RUN_DIR="$(mktemp -d)"
LOG_DIR="${RUN_DIR}/logs"
WORK_DIR="${RUN_DIR}/work"
FLOW_LOG_DIR="${RUN_DIR}/flow-logs"
CONFIG_DIR="${RUN_DIR}/config"
mkdir -p "${LOG_DIR}" "${WORK_DIR}" "${CONFIG_DIR}" "${FLOW_LOG_DIR}"

PIDS=()
REDIS_CONTAINER=""
RUN_SUCCEEDED=0

cleanup() {
  find "${RUN_DIR}" -type f -name "*.json" -delete 2>/dev/null || true
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
    docker rm -fv "${REDIS_CONTAINER}" >/dev/null 2>&1 || true
  fi
  if [[ "${RUN_SUCCEEDED}" == "1" ]]; then
    rm -rf "${RUN_DIR}"
  else
    echo "runDir=${RUN_DIR}"
  fi
}
trap cleanup EXIT

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

REDIS_KEY_PREFIX="deliverydispatch:dotnet:${RANDOM}:$$:"
DISPATCH_HTTP="http://127.0.0.1:${PORTS[2]}"
DISPATCH_CHANNEL="tcp://127.0.0.1:${PORTS[3]}"
DISPATCH_SPOT_ROUTER="tcp://127.0.0.1:${PORTS[4]}"
TRACKING_CHANNEL="tcp://127.0.0.1:${PORTS[5]}"
TRACKING_SPOT_ROUTER="tcp://127.0.0.1:${PORTS[6]}"
TRACKING_SPOT="tcp://127.0.0.1:${PORTS[7]}"
CUSTOMER_STREAM="tcp://127.0.0.1:${PORTS[8]}"
CUSTOMER_SPOT_ROUTER="tcp://127.0.0.1:${PORTS[9]}"
CUSTOMER_SPOT="tcp://127.0.0.1:${PORTS[10]}"
COURIER_STREAM="tcp://127.0.0.1:${PORTS[11]}"
COURIER_SESSION_SPOT_ROUTER="tcp://127.0.0.1:${PORTS[12]}"
COURIER_SESSION_SPOT="tcp://127.0.0.1:${PORTS[13]}"
COURIER_NODE1_ROUTER="tcp://127.0.0.1:${PORTS[14]}"
COURIER_NODE1="tcp://127.0.0.1:${PORTS[15]}"
COURIER_NODE2_ROUTER="tcp://127.0.0.1:${PORTS[17]}"
COURIER_NODE2="tcp://127.0.0.1:${PORTS[18]}"

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

zlink_redis_start_scoped_assign REDIS_CONTAINER DELIVERYDISPATCH_REDIS_ENDPOINT "zlink-deliverydispatch-dotnet-redis" redis:7.2-alpine
REDIS_ENDPOINT="${DELIVERYDISPATCH_REDIS_ENDPOINT}"
wait_port redis "tcp://${REDIS_ENDPOINT}"

# Each role gets one configuration file holding the values it needs. The runner picks this run's
# ports, but it hands them over in a file rather than through the environment
# (framework/doc/framework/common/sample-e2e-configuration-policy.ko.md 2.2, 7).
write_role_config() {
  local role="$1"
  local node_rid="${2:-}"
  python3 "${SCRIPT_DIR}/write_role_config.py" \
    --output "${CONFIG_DIR}/${role}.json" \
    --role "${role}" \
    --node-rid "${node_rid}" \
    --log-dir "${FLOW_LOG_DIR}" \
    --work-dir "${WORK_DIR}" \
    --redis-endpoint "${REDIS_ENDPOINT}" \
    --redis-key-prefix "${REDIS_KEY_PREFIX}" \
    --dispatch-http "${DISPATCH_HTTP}" \
    --dispatch-channel "${DISPATCH_CHANNEL}" \
    --dispatch-spot-router "${DISPATCH_SPOT_ROUTER}" \
    --tracking-channel "${TRACKING_CHANNEL}" \
    --tracking-spot-router "${TRACKING_SPOT_ROUTER}" \
    --tracking-spot "${TRACKING_SPOT}" \
    --customer-stream "${CUSTOMER_STREAM}" \
    --customer-spot-router "${CUSTOMER_SPOT_ROUTER}" \
    --customer-spot "${CUSTOMER_SPOT}" \
    --courier-stream "${COURIER_STREAM}" \
    --courier-session-spot-router "${COURIER_SESSION_SPOT_ROUTER}" \
    --courier-session-spot "${COURIER_SESSION_SPOT}" \
    --courier-node1-router "${COURIER_NODE1_ROUTER}" \
    --courier-node1 "${COURIER_NODE1}" \
    --courier-node2-router "${COURIER_NODE2_ROUTER}" \
    --courier-node2 "${COURIER_NODE2}"
}

write_role_config tracking
write_role_config customer-gateway
write_role_config courier-session
write_role_config dispatch
write_role_config courier-actor-node1 delivery-courier-node-1
write_role_config courier-actor-node2 delivery-courier-node-2
write_role_config client

dotnet build "${SCRIPT_DIR}/DeliveryDispatch.sln" --maxcpucount:1

start_server tracking "${SCRIPT_DIR}/Server/Tracking/DeliveryDispatch.Server.Tracking.csproj" --config "${CONFIG_DIR}/tracking.json"
wait_port tracking-channel "${TRACKING_CHANNEL}"
wait_port tracking-spot-router "${TRACKING_SPOT_ROUTER}"

start_server customer-gateway "${SCRIPT_DIR}/Server/CustomerGateway/DeliveryDispatch.Server.CustomerGateway.csproj" --config "${CONFIG_DIR}/customer-gateway.json"
wait_port customer-stream "${CUSTOMER_STREAM}"
wait_port customer-spot-router "${CUSTOMER_SPOT_ROUTER}"

start_server courier-actor-node1 "${SCRIPT_DIR}/Server/CourierActorNode/DeliveryDispatch.Server.CourierActorNode.csproj" --config "${CONFIG_DIR}/courier-actor-node1.json"
wait_port courier-actor-node1-router "${COURIER_NODE1_ROUTER}"

start_server courier-actor-node2 "${SCRIPT_DIR}/Server/CourierActorNode/DeliveryDispatch.Server.CourierActorNode.csproj" --config "${CONFIG_DIR}/courier-actor-node2.json"
wait_port courier-actor-node2-router "${COURIER_NODE2_ROUTER}"

start_server courier-session "${SCRIPT_DIR}/Server/CourierSession/DeliveryDispatch.Server.CourierSession.csproj" --config "${CONFIG_DIR}/courier-session.json"
wait_port courier-session-router "${COURIER_SESSION_SPOT_ROUTER}"
wait_port courier-session-stream "${COURIER_STREAM}"

start_server dispatch "${SCRIPT_DIR}/Server/Dispatch/DeliveryDispatch.Server.Dispatch.csproj" --config "${CONFIG_DIR}/dispatch.json"
wait_port dispatch-spot-router "${DISPATCH_SPOT_ROUTER}"
wait_http dispatch "${DISPATCH_HTTP}"

dotnet run --no-build --project "${SCRIPT_DIR}/Client/DeliveryDispatch.Client.csproj" -- \
  --config "${CONFIG_DIR}/client.json" >"${LOG_DIR}/client.log" 2>&1

grep -q "deliverydispatch=completed" "${LOG_DIR}/client.log"
grep -q "topology=ready" "${LOG_DIR}/client.log"
grep -q "deliverydispatch-reassignment=completed" "${LOG_DIR}/client.log"
wait_log "deliverydispatch tracking: status" "${LOG_DIR}/tracking.log"
wait_log "deliverydispatch customer-session: bound customer" "${LOG_DIR}/customer-gateway.log"
wait_log "deliverydispatch customer-entry: pushed status" "${LOG_DIR}/customer-gateway.log"
wait_log "deliverydispatch courier-session: bound courier=courier-a" "${LOG_DIR}/courier-session.log"
wait_log "deliverydispatch courier-session: bound courier=courier-b" "${LOG_DIR}/courier-session.log"
grep -Rq "message flow" "${FLOW_LOG_DIR}"
echo "deliverydispatch-runner-evidence=completed"
RUN_SUCCEEDED=1
