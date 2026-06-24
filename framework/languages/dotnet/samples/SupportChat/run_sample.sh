#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_DIR="$(mktemp -d)"
LOG_DIR="${RUN_DIR}/logs"
export SUPPORTCHAT_LOG_DIR="${SUPPORTCHAT_LOG_DIR:-${SCRIPT_DIR}/logs}"
mkdir -p "${LOG_DIR}" "${SUPPORTCHAT_LOG_DIR}"
rm -f "${SUPPORTCHAT_LOG_DIR}"/*.log

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
  for ((i=${#PIDS[@]}-1; i>=0; i--)); do
    local pid="${PIDS[$i]}"
    if kill -0 "${pid}" 2>/dev/null; then
      kill -9 "${pid}" 2>/dev/null || true
    fi
  done
  for pid in "${PIDS[@]}"; do
    wait "${pid}" 2>/dev/null || true
  done
  if [[ "${SUPPORTCHAT_KEEP_RUN_DIR:-}" != "1" ]]; then
    rm -rf "${RUN_DIR}"
  else
    echo "runDir=${RUN_DIR}"
  fi
}
trap cleanup EXIT

if [[ -n "${SUPPORTCHAT_BASE_PORT:-}" ]]; then
  PORTS=()
  for offset in $(seq 1 13); do
    PORTS+=("$((SUPPORTCHAT_BASE_PORT + offset))")
  done
else
  read -r -a PORTS <<<"$(python3 - <<'PY'
import random
import socket

sockets = []
try:
    chosen = set()
    while len(sockets) < 13:
        port = random.randint(48000, 60999)
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

export SUPPORTCHAT_REGISTRY_PUB_ENDPOINT="${SUPPORTCHAT_REGISTRY_PUB_ENDPOINT:-tcp://127.0.0.1:${PORTS[0]}}"
export SUPPORTCHAT_REGISTRY_ROUTER_ENDPOINT="${SUPPORTCHAT_REGISTRY_ROUTER_ENDPOINT:-tcp://127.0.0.1:${PORTS[1]}}"
export SUPPORTCHAT_API_CHANNEL_ENDPOINT="${SUPPORTCHAT_API_CHANNEL_ENDPOINT:-tcp://127.0.0.1:${PORTS[2]}}"
export SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT="${SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT:-tcp://127.0.0.1:${PORTS[3]}}"
export SUPPORTCHAT_SESSION_SPOT_ENDPOINT="${SUPPORTCHAT_SESSION_SPOT_ENDPOINT:-tcp://127.0.0.1:${PORTS[4]}}"
export SUPPORTCHAT_SESSION_ROUTER_ENDPOINT="${SUPPORTCHAT_SESSION_ROUTER_ENDPOINT:-tcp://127.0.0.1:${PORTS[5]}}"
export SUPPORTCHAT_SUPPORT_ROUTER_ENDPOINT="${SUPPORTCHAT_SUPPORT_ROUTER_ENDPOINT:-tcp://127.0.0.1:${PORTS[6]}}"
export SUPPORTCHAT_ENTRY_SPOT_ENDPOINT="${SUPPORTCHAT_ENTRY_SPOT_ENDPOINT:-tcp://127.0.0.1:${PORTS[7]}}"
export SUPPORTCHAT_ENTRY_SPOT_ROUTER_ENDPOINT="${SUPPORTCHAT_ENTRY_SPOT_ROUTER_ENDPOINT:-tcp://127.0.0.1:${PORTS[8]}}"
export SUPPORTCHAT_CONVERSATION_SPOT_ENDPOINT="${SUPPORTCHAT_CONVERSATION_SPOT_ENDPOINT:-tcp://127.0.0.1:${PORTS[9]}}"
export SUPPORTCHAT_CONVERSATION_SPOT_ROUTER_ENDPOINT="${SUPPORTCHAT_CONVERSATION_SPOT_ROUTER_ENDPOINT:-tcp://127.0.0.1:${PORTS[10]}}"
export SUPPORTCHAT_STREAM_ENDPOINT="${SUPPORTCHAT_STREAM_ENDPOINT:-tcp://127.0.0.1:${PORTS[11]}}"
export SUPPORTCHAT_RECONNECT_STREAM_ENDPOINT="${SUPPORTCHAT_RECONNECT_STREAM_ENDPOINT:-tcp://127.0.0.1:${PORTS[12]}}"

endpoint_host() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  echo "${endpoint%:*}"
}

endpoint_port() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
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

start_server() {
  local name="$1"
  local project="$2"
  local project_dir
  local project_name
  local assembly
  project_dir="$(cd "$(dirname "${project}")" && pwd)"
  project_name="$(basename "${project}" .csproj)"
  assembly="${project_dir}/bin/Debug/net8.0/${project_name}.dll"
  dotnet "${assembly}" >"${LOG_DIR}/${name}.log" 2>&1 &
  PIDS+=("$!")
}

dotnet build "${SCRIPT_DIR}/SupportChat.csproj" --maxcpucount:1

start_server registry "${SCRIPT_DIR}/Server/Registry/SupportChat.Server.Registry.csproj"
wait_port registry-router "${SUPPORTCHAT_REGISTRY_ROUTER_ENDPOINT}"

start_server support "${SCRIPT_DIR}/Server/Support/SupportChat.Server.Support.csproj"
wait_port support-channel "${SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT}"
wait_port support-spot-router "${SUPPORTCHAT_ENTRY_SPOT_ROUTER_ENDPOINT}"
wait_port support-spot-pub "${SUPPORTCHAT_ENTRY_SPOT_ENDPOINT}"

start_server api "${SCRIPT_DIR}/Server/Api/SupportChat.Server.Api.csproj"
wait_port api "${SUPPORTCHAT_API_CHANNEL_ENDPOINT}"

start_server session "${SCRIPT_DIR}/Server/Session/SupportChat.Server.Session.csproj"
wait_port session-route "${SUPPORTCHAT_SESSION_SPOT_ENDPOINT}"
wait_port session-router "${SUPPORTCHAT_SESSION_ROUTER_ENDPOINT}"
wait_port session-stream "${SUPPORTCHAT_STREAM_ENDPOINT}"

dotnet run --no-build --project "${SCRIPT_DIR}/Probe/SupportChat.Probe.csproj" -- \
  --registry-endpoint "${SUPPORTCHAT_REGISTRY_ROUTER_ENDPOINT}" \
  --timeout-seconds 10

dotnet run --no-build --project "${SCRIPT_DIR}/Client/SupportChat.Client.csproj" -- \
  --stream-endpoint "${SUPPORTCHAT_STREAM_ENDPOINT}"

grep -q "support conversation: created" "${LOG_DIR}/support.log"
grep -q "support conversation: actor joined" "${LOG_DIR}/support.log"
grep -Rq "message flow" "${SUPPORTCHAT_LOG_DIR}"
echo "supportchat-server-evidence=completed"
