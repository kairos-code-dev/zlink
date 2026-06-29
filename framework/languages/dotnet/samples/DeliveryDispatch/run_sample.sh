#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_DIR="$(mktemp -d)"
LOG_DIR="${RUN_DIR}/logs"
WORK_DIR="${RUN_DIR}/work"
export DELIVERYDISPATCH_LOG_DIR="${DELIVERYDISPATCH_LOG_DIR:-${SCRIPT_DIR}/logs}"
mkdir -p "${LOG_DIR}" "${WORK_DIR}" "${DELIVERYDISPATCH_LOG_DIR}"
rm -f "${DELIVERYDISPATCH_LOG_DIR}"/*.log

PIDS=()

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
  if [[ "${DELIVERYDISPATCH_KEEP_RUN_DIR:-}" != "1" ]]; then
    rm -rf "${RUN_DIR}"
  else
    echo "runDir=${RUN_DIR}"
  fi
}
trap cleanup EXIT

if [[ -n "${DELIVERYDISPATCH_BASE_PORT:-}" ]]; then
  PORTS=()
  for offset in $(seq 1 16); do
    PORTS+=("$((DELIVERYDISPATCH_BASE_PORT + offset))")
  done
else
  read -r -a PORTS <<<"$(python3 - <<'PY'
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
)"
fi

export DELIVERYDISPATCH_REGISTRY_PUB="tcp://127.0.0.1:${PORTS[0]}"
export DELIVERYDISPATCH_REGISTRY="tcp://127.0.0.1:${PORTS[1]}"
export DELIVERYDISPATCH_API_HTTP="http://127.0.0.1:${PORTS[2]}"
export DELIVERYDISPATCH_CENTER_ROUTE="tcp://127.0.0.1:${PORTS[3]}"
export DELIVERYDISPATCH_COURIER_A_ROUTE="tcp://127.0.0.1:${PORTS[4]}"
export DELIVERYDISPATCH_COURIER_B_ROUTE="tcp://127.0.0.1:${PORTS[5]}"
export DELIVERYDISPATCH_TRACKING_ROUTE="tcp://127.0.0.1:${PORTS[6]}"
export DELIVERYDISPATCH_STATUS_FANOUT="tcp://127.0.0.1:${PORTS[7]}"
export DELIVERYDISPATCH_TRACKING_SPOT_ROUTER="tcp://127.0.0.1:${PORTS[8]}"
export DELIVERYDISPATCH_TRACKING_SPOT="tcp://127.0.0.1:${PORTS[9]}"
export DELIVERYDISPATCH_SESSION_STREAM="tcp://127.0.0.1:${PORTS[10]}"
export DELIVERYDISPATCH_SESSION_SPOT_ROUTER="tcp://127.0.0.1:${PORTS[11]}"
export DELIVERYDISPATCH_SESSION_SPOT="tcp://127.0.0.1:${PORTS[12]}"
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

dotnet build "${SCRIPT_DIR}/DeliveryDispatch.csproj" --maxcpucount:1

start_server registry "${SCRIPT_DIR}/Server/Registry/DeliveryDispatch.Server.Registry.csproj"
wait_port registry-router "${DELIVERYDISPATCH_REGISTRY}"

start_server tracking "${SCRIPT_DIR}/Server/Tracking/DeliveryDispatch.Server.Tracking.csproj"
wait_port tracking-route "${DELIVERYDISPATCH_TRACKING_ROUTE}"
wait_port status-fanout "${DELIVERYDISPATCH_STATUS_FANOUT}"

start_server session "${SCRIPT_DIR}/Server/Session/DeliveryDispatch.Server.Session.csproj"
wait_port session-stream "${DELIVERYDISPATCH_SESSION_STREAM}"

start_server courier-a "${SCRIPT_DIR}/Server/Courier/DeliveryDispatch.Server.Courier.csproj" --courier courier-a --mode timeout-reassign
wait_port courier-a "${DELIVERYDISPATCH_COURIER_A_ROUTE}"

start_server courier-b "${SCRIPT_DIR}/Server/Courier/DeliveryDispatch.Server.Courier.csproj" --courier courier-b --mode accept
wait_port courier-b "${DELIVERYDISPATCH_COURIER_B_ROUTE}"

start_server dispatch-center "${SCRIPT_DIR}/Server/DispatchCenter/DeliveryDispatch.Server.DispatchCenter.csproj"
wait_port dispatch-center "${DELIVERYDISPATCH_CENTER_ROUTE}"

start_server dispatch-api "${SCRIPT_DIR}/Server/DispatchApi/DeliveryDispatch.Server.DispatchApi.csproj"
wait_http dispatch-api "${DELIVERYDISPATCH_API_HTTP}"

sleep "${DELIVERYDISPATCH_STARTUP_DELAY_SECONDS:-3}"

dotnet run --no-build --project "${SCRIPT_DIR}/Client/DeliveryDispatch.Client.csproj" -- \
  --api-url "${DELIVERYDISPATCH_API_HTTP}" \
  --stream-endpoint "${DELIVERYDISPATCH_SESSION_STREAM}"

grep -q "deliverydispatch tracking: status" "${LOG_DIR}/tracking.log"
grep -q "deliverydispatch session: bound customer" "${LOG_DIR}/session.log"
grep -Rq "message flow" "${DELIVERYDISPATCH_LOG_DIR}"
