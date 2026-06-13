#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_DIR="$(mktemp -d)"
LOG_DIR="${RUN_DIR}/logs"
mkdir -p "${LOG_DIR}"

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
  if [[ "${BINGO_KEEP_RUN_DIR:-}" != "1" ]]; then
    rm -rf "${RUN_DIR}"
  else
    echo "runDir=${RUN_DIR}"
  fi
}
trap cleanup EXIT

if [[ -n "${BINGO_BASE_PORT:-}" ]]; then
  PORTS=()
  for offset in $(seq 1 13); do
    PORTS+=("$((BINGO_BASE_PORT + offset))")
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

export BINGO_REGISTRY_PUB_ENDPOINT="${BINGO_REGISTRY_PUB_ENDPOINT:-tcp://127.0.0.1:${PORTS[0]}}"
export BINGO_REGISTRY_ROUTER_ENDPOINT="${BINGO_REGISTRY_ROUTER_ENDPOINT:-tcp://127.0.0.1:${PORTS[1]}}"
export BINGO_API_CHANNEL_ENDPOINT="${BINGO_API_CHANNEL_ENDPOINT:-tcp://127.0.0.1:${PORTS[2]}}"
export BINGO_PLAY_CHANNEL_ENDPOINT="${BINGO_PLAY_CHANNEL_ENDPOINT:-tcp://127.0.0.1:${PORTS[3]}}"
export BINGO_SESSION_SPOT_ENDPOINT="${BINGO_SESSION_SPOT_ENDPOINT:-tcp://127.0.0.1:${PORTS[4]}}"
export BINGO_SESSION_ROUTER_ENDPOINT="${BINGO_SESSION_ROUTER_ENDPOINT:-tcp://127.0.0.1:${PORTS[5]}}"
export BINGO_RECONNECT_SESSION_SPOT_ENDPOINT="${BINGO_RECONNECT_SESSION_SPOT_ENDPOINT:-tcp://127.0.0.1:${PORTS[6]}}"
export BINGO_RECONNECT_SESSION_ROUTER_ENDPOINT="${BINGO_RECONNECT_SESSION_ROUTER_ENDPOINT:-tcp://127.0.0.1:${PORTS[7]}}"
export BINGO_PLAY_ROUTER_ENDPOINT="${BINGO_PLAY_ROUTER_ENDPOINT:-tcp://127.0.0.1:${PORTS[8]}}"
export BINGO_PLAY_SPOT_ENDPOINT="${BINGO_PLAY_SPOT_ENDPOINT:-tcp://127.0.0.1:${PORTS[9]}}"
export BINGO_PLAY_SPOT_ROUTER_ENDPOINT="${BINGO_PLAY_SPOT_ROUTER_ENDPOINT:-tcp://127.0.0.1:${PORTS[10]}}"
export BINGO_STREAM_ENDPOINT="${BINGO_STREAM_ENDPOINT:-tcp://127.0.0.1:${PORTS[11]}}"
export BINGO_RECONNECT_STREAM_ENDPOINT="${BINGO_RECONNECT_STREAM_ENDPOINT:-tcp://127.0.0.1:${PORTS[12]}}"
export BINGO_METADATA_DIR="${BINGO_METADATA_DIR:-${RUN_DIR}/metadata}"

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

dotnet build "${SCRIPT_DIR}/Bingo.csproj" --maxcpucount:1

start_server registry "${SCRIPT_DIR}/Server/Registry/Bingo.Server.Registry.csproj"
wait_port registry-router "${BINGO_REGISTRY_ROUTER_ENDPOINT}"

start_server api "${SCRIPT_DIR}/Server/Api/Bingo.Server.Api.csproj"
wait_port api "${BINGO_API_CHANNEL_ENDPOINT}"

start_server play "${SCRIPT_DIR}/Server/Play/Bingo.Server.Play.csproj"
wait_port play "${BINGO_PLAY_CHANNEL_ENDPOINT}"
wait_port play-spot-router "${BINGO_PLAY_SPOT_ROUTER_ENDPOINT}"
wait_port play-spot-pub "${BINGO_PLAY_SPOT_ENDPOINT}"

start_server session "${SCRIPT_DIR}/Server/Session/Bingo.Server.Session.csproj"
wait_port session-route "${BINGO_SESSION_SPOT_ENDPOINT}"
wait_port session-router "${BINGO_SESSION_ROUTER_ENDPOINT}"
wait_port session-stream "${BINGO_STREAM_ENDPOINT}"

dotnet run --no-build --project "${SCRIPT_DIR}/Probe/Bingo.Probe.csproj" -- \
  --registry-endpoint "${BINGO_REGISTRY_ROUTER_ENDPOINT}" \
  --timeout-seconds 10

dotnet run --no-build --project "${SCRIPT_DIR}/Client/Bingo.Client.csproj" -- \
  --stream-endpoint "${BINGO_STREAM_ENDPOINT}"

grep -q "bingo room: actor left. room=.*actor=player-1" "${LOG_DIR}/play.log"
grep -q "bingo room: actor left. room=.*actor=player-2" "${LOG_DIR}/play.log"
grep -q "entry spot: actor destroy completed. actor=player-1" "${LOG_DIR}/play.log"
grep -q "entry spot: actor destroy completed. actor=player-2" "${LOG_DIR}/play.log"
