#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_DIR="$(mktemp -d)"
LOG_DIR="${RUN_DIR}/logs"
STORE_DIR="${RUN_DIR}/store"
mkdir -p "${LOG_DIR}" "${STORE_DIR}"

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
  if [[ "${SHOPPINGMALL_KEEP_RUN_DIR:-}" != "1" ]]; then
    rm -rf "${RUN_DIR}"
  else
    echo "runDir=${RUN_DIR}"
  fi
}
trap cleanup EXIT

if [[ -n "${SHOPPINGMALL_BASE_PORT:-}" ]]; then
  PORTS=()
  for offset in $(seq 1 14); do
    PORTS+=("$((SHOPPINGMALL_BASE_PORT + offset))")
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

export SHOPPINGMALL_REGISTRY_PUB_ENDPOINT="${SHOPPINGMALL_REGISTRY_PUB_ENDPOINT:-tcp://127.0.0.1:${PORTS[0]}}"
export SHOPPINGMALL_REGISTRY_ROUTER_ENDPOINT="${SHOPPINGMALL_REGISTRY_ROUTER_ENDPOINT:-tcp://127.0.0.1:${PORTS[1]}}"
export SHOPPINGMALL_API_A_HTTP_URL="${SHOPPINGMALL_API_A_HTTP_URL:-http://127.0.0.1:${PORTS[2]}}"
export SHOPPINGMALL_API_B_HTTP_URL="${SHOPPINGMALL_API_B_HTTP_URL:-http://127.0.0.1:${PORTS[3]}}"
export SHOPPINGMALL_API_A_ROUTE_ENDPOINT="${SHOPPINGMALL_API_A_ROUTE_ENDPOINT:-tcp://127.0.0.1:${PORTS[4]}}"
export SHOPPINGMALL_API_B_ROUTE_ENDPOINT="${SHOPPINGMALL_API_B_ROUTE_ENDPOINT:-tcp://127.0.0.1:${PORTS[5]}}"
export SHOPPINGMALL_WORKFLOW_A_HTTP_URL="${SHOPPINGMALL_WORKFLOW_A_HTTP_URL:-http://127.0.0.1:${PORTS[6]}}"
export SHOPPINGMALL_WORKFLOW_B_HTTP_URL="${SHOPPINGMALL_WORKFLOW_B_HTTP_URL:-http://127.0.0.1:${PORTS[7]}}"
export SHOPPINGMALL_WORKFLOW_A_ROUTE_ENDPOINT="${SHOPPINGMALL_WORKFLOW_A_ROUTE_ENDPOINT:-tcp://127.0.0.1:${PORTS[8]}}"
export SHOPPINGMALL_WORKFLOW_B_ROUTE_ENDPOINT="${SHOPPINGMALL_WORKFLOW_B_ROUTE_ENDPOINT:-tcp://127.0.0.1:${PORTS[9]}}"
export SHOPPINGMALL_WORKFLOW_A_SPOT_ENDPOINT="${SHOPPINGMALL_WORKFLOW_A_SPOT_ENDPOINT:-tcp://127.0.0.1:${PORTS[10]}}"
export SHOPPINGMALL_WORKFLOW_A_SPOT_ROUTER_ENDPOINT="${SHOPPINGMALL_WORKFLOW_A_SPOT_ROUTER_ENDPOINT:-tcp://127.0.0.1:${PORTS[11]}}"
export SHOPPINGMALL_WORKFLOW_B_SPOT_ENDPOINT="${SHOPPINGMALL_WORKFLOW_B_SPOT_ENDPOINT:-tcp://127.0.0.1:${PORTS[12]}}"
export SHOPPINGMALL_WORKFLOW_B_SPOT_ROUTER_ENDPOINT="${SHOPPINGMALL_WORKFLOW_B_SPOT_ROUTER_ENDPOINT:-tcp://127.0.0.1:${PORTS[13]}}"
export SHOPPINGMALL_STORE_DIR="${SHOPPINGMALL_STORE_DIR:-${STORE_DIR}}"

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

wait_port() {
  local name="$1"
  local endpoint="$2"
  local host
  local port
  host="$(endpoint_host "${endpoint}")"
  port="$(endpoint_port "${endpoint}")"
  for _ in $(seq 1 100); do
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
  for _ in $(seq 1 100); do
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

dotnet build "${SCRIPT_DIR}/ShoppingMallCheckout.csproj" --maxcpucount:1

start_server registry "${SCRIPT_DIR}/Server/Registry/ShoppingMall.Registry.csproj"
wait_port registry-router "${SHOPPINGMALL_REGISTRY_ROUTER_ENDPOINT}"

start_server workflow-a "${SCRIPT_DIR}/Server/OrderWorkflow/ShoppingMall.OrderWorkflow.csproj" --instance workflow-a
wait_port workflow-a-route "${SHOPPINGMALL_WORKFLOW_A_ROUTE_ENDPOINT}"
wait_port workflow-a-spot-router "${SHOPPINGMALL_WORKFLOW_A_SPOT_ROUTER_ENDPOINT}"
wait_port workflow-a-spot-pub "${SHOPPINGMALL_WORKFLOW_A_SPOT_ENDPOINT}"
wait_http workflow-a "${SHOPPINGMALL_WORKFLOW_A_HTTP_URL}"

start_server workflow-b "${SCRIPT_DIR}/Server/OrderWorkflow/ShoppingMall.OrderWorkflow.csproj" --instance workflow-b
wait_port workflow-b-route "${SHOPPINGMALL_WORKFLOW_B_ROUTE_ENDPOINT}"
wait_port workflow-b-spot-router "${SHOPPINGMALL_WORKFLOW_B_SPOT_ROUTER_ENDPOINT}"
wait_port workflow-b-spot-pub "${SHOPPINGMALL_WORKFLOW_B_SPOT_ENDPOINT}"
wait_http workflow-b "${SHOPPINGMALL_WORKFLOW_B_HTTP_URL}"

start_server api-a "${SCRIPT_DIR}/Server/CommerceApi/ShoppingMall.CommerceApi.csproj" --instance api-a
wait_port api-a-route "${SHOPPINGMALL_API_A_ROUTE_ENDPOINT}"
wait_http api-a "${SHOPPINGMALL_API_A_HTTP_URL}"

start_server api-b "${SCRIPT_DIR}/Server/CommerceApi/ShoppingMall.CommerceApi.csproj" --instance api-b
wait_port api-b-route "${SHOPPINGMALL_API_B_ROUTE_ENDPOINT}"
wait_http api-b "${SHOPPINGMALL_API_B_HTTP_URL}"

dotnet run --no-build --project "${SCRIPT_DIR}/Client/ShoppingMallCheckout.Client.csproj"

grep -q "shoppingmall order: started" "${LOG_DIR}/workflow-a.log"
grep -q "shoppingmall order: started" "${LOG_DIR}/workflow-b.log"
grep -q "shoppingmall evidence:" "${LOG_DIR}/api-a.log"
echo "shoppingmall-server-evidence=completed"
