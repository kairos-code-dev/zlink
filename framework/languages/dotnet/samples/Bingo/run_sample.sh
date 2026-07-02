#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_DIR="$(mktemp -d)"
LOG_DIR="${RUN_DIR}/logs"
export BINGO_LOG_DIR="${BINGO_LOG_DIR:-${SCRIPT_DIR}/logs}"
mkdir -p "${LOG_DIR}" "${BINGO_LOG_DIR}"
rm -f "${BINGO_LOG_DIR}"/*.log

PIDS=()
REDIS_CONTAINER=""
export BINGO_REDIS_KEY_PREFIX="${BINGO_REDIS_KEY_PREFIX:-bingo:dotnet:${RANDOM}:$$:}"

cleanup() {
  set +e
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
  if [[ -n "${REDIS_CONTAINER}" ]]; then
    docker rm -f "${REDIS_CONTAINER}" >/dev/null 2>&1 || true
  fi
  if [[ "${BINGO_KEEP_RUN_DIR:-}" != "1" ]]; then
    rm -rf "${RUN_DIR}"
  else
    echo "runDir=${RUN_DIR}"
  fi
}
trap cleanup EXIT

if [[ -n "${BINGO_BASE_PORT:-}" ]]; then
  PORTS=()
  for offset in $(seq 1 22); do
    PORTS+=("$((BINGO_BASE_PORT + offset))")
  done
else
  read -r -a PORTS <<<"$(python3 - <<'PY'
import random
import socket

sockets = []
try:
    chosen = set()
    while len(sockets) < 22:
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

export BINGO_API_A_CHANNEL_ENDPOINT="${BINGO_API_A_CHANNEL_ENDPOINT:-tcp://127.0.0.1:${PORTS[2]}}"
export BINGO_PLAY_A_CHANNEL_ENDPOINT="${BINGO_PLAY_A_CHANNEL_ENDPOINT:-tcp://127.0.0.1:${PORTS[3]}}"
export BINGO_SESSION_A_SPOT_ENDPOINT="${BINGO_SESSION_A_SPOT_ENDPOINT:-tcp://127.0.0.1:${PORTS[4]}}"
export BINGO_SESSION_A_ROUTER_ENDPOINT="${BINGO_SESSION_A_ROUTER_ENDPOINT:-tcp://127.0.0.1:${PORTS[5]}}"
export BINGO_SESSION_B_SPOT_ENDPOINT="${BINGO_SESSION_B_SPOT_ENDPOINT:-tcp://127.0.0.1:${PORTS[6]}}"
export BINGO_SESSION_B_ROUTER_ENDPOINT="${BINGO_SESSION_B_ROUTER_ENDPOINT:-tcp://127.0.0.1:${PORTS[7]}}"
export BINGO_PLAY_B_CHANNEL_ENDPOINT="${BINGO_PLAY_B_CHANNEL_ENDPOINT:-tcp://127.0.0.1:${PORTS[8]}}"
export BINGO_PLAY_A_SPOT_ENDPOINT="${BINGO_PLAY_A_SPOT_ENDPOINT:-tcp://127.0.0.1:${PORTS[9]}}"
export BINGO_PLAY_A_SPOT_ROUTER_ENDPOINT="${BINGO_PLAY_A_SPOT_ROUTER_ENDPOINT:-tcp://127.0.0.1:${PORTS[10]}}"
export BINGO_SESSION_A_STREAM_ENDPOINT="${BINGO_SESSION_A_STREAM_ENDPOINT:-tcp://127.0.0.1:${PORTS[11]}}"
export BINGO_SESSION_B_STREAM_ENDPOINT="${BINGO_SESSION_B_STREAM_ENDPOINT:-tcp://127.0.0.1:${PORTS[12]}}"
export BINGO_PLAY_B_SPOT_ENDPOINT="${BINGO_PLAY_B_SPOT_ENDPOINT:-tcp://127.0.0.1:${PORTS[13]}}"
export BINGO_PLAY_B_SPOT_ROUTER_ENDPOINT="${BINGO_PLAY_B_SPOT_ROUTER_ENDPOINT:-tcp://127.0.0.1:${PORTS[14]}}"
export BINGO_API_B_CHANNEL_ENDPOINT="${BINGO_API_B_CHANNEL_ENDPOINT:-tcp://127.0.0.1:${PORTS[15]}}"
export BINGO_API_A_PLAY_ROUTE_ENDPOINT="${BINGO_API_A_PLAY_ROUTE_ENDPOINT:-tcp://127.0.0.1:${PORTS[17]}}"
export BINGO_API_B_PLAY_ROUTE_ENDPOINT="${BINGO_API_B_PLAY_ROUTE_ENDPOINT:-tcp://127.0.0.1:${PORTS[18]}}"
export BINGO_SESSION_A_PLAY_ROUTE_ENDPOINT="${BINGO_SESSION_A_PLAY_ROUTE_ENDPOINT:-tcp://127.0.0.1:${PORTS[19]}}"
export BINGO_SESSION_B_PLAY_ROUTE_ENDPOINT="${BINGO_SESSION_B_PLAY_ROUTE_ENDPOINT:-tcp://127.0.0.1:${PORTS[20]}}"
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

require_log_count() {
  local expected="$1"
  local pattern="$2"
  local file="$3"
  local actual
  actual="$(grep -Ec "${pattern}" "${file}" || true)"
  if [[ "${actual}" != "${expected}" ]]; then
    echo "Expected ${expected} matches for '${pattern}' in ${file}, found ${actual}." >&2
    return 1
  fi
}

# The sample owns its Redis: always provision a dedicated, throwaway container
# so room-allocation state stays isolated per run and never touches a developer's
# local Redis. (BINGO_REDIS_ENDPOINT is intentionally derived here, not read.)
if ! command -v docker >/dev/null 2>&1; then
  echo "Docker is required to run the Bingo sample (it provisions a dedicated Redis container)." >&2
  exit 1
fi
REDIS_CONTAINER="bingo-dotnet-redis-${RANDOM}-$$"
docker run -d --rm --name "${REDIS_CONTAINER}" -p "127.0.0.1::6379" redis:7.2-alpine >/dev/null
export BINGO_REDIS_ENDPOINT="$(docker port "${REDIS_CONTAINER}" 6379/tcp | sed -E 's/.*:([0-9]+)$/127.0.0.1:\1/')"
wait_port redis "tcp://${BINGO_REDIS_ENDPOINT}"

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

dotnet build "${SCRIPT_DIR}/Bingo.csproj" --maxcpucount:1

start_server play-a "${SCRIPT_DIR}/Server/Play/Bingo.Server.Play.csproj" --node a
wait_port play-a "${BINGO_PLAY_A_CHANNEL_ENDPOINT}"
wait_port play-a-spot-router "${BINGO_PLAY_A_SPOT_ROUTER_ENDPOINT}"
wait_port play-a-spot-pub "${BINGO_PLAY_A_SPOT_ENDPOINT}"
start_server play-b "${SCRIPT_DIR}/Server/Play/Bingo.Server.Play.csproj" --node b
wait_port play-b "${BINGO_PLAY_B_CHANNEL_ENDPOINT}"
wait_port play-b-spot-router "${BINGO_PLAY_B_SPOT_ROUTER_ENDPOINT}"
wait_port play-b-spot-pub "${BINGO_PLAY_B_SPOT_ENDPOINT}"

start_server api-a "${SCRIPT_DIR}/Server/Api/Bingo.Server.Api.csproj" --node a
wait_port api-a "${BINGO_API_A_CHANNEL_ENDPOINT}"
wait_port api-a-play-route "${BINGO_API_A_PLAY_ROUTE_ENDPOINT}"
start_server api-b "${SCRIPT_DIR}/Server/Api/Bingo.Server.Api.csproj" --node b
wait_port api-b "${BINGO_API_B_CHANNEL_ENDPOINT}"
wait_port api-b-play-route "${BINGO_API_B_PLAY_ROUTE_ENDPOINT}"

start_server session-a "${SCRIPT_DIR}/Server/Session/Bingo.Server.Session.csproj" --node a
wait_port session-a-router "${BINGO_SESSION_A_ROUTER_ENDPOINT}"
wait_port session-a-stream "${BINGO_SESSION_A_STREAM_ENDPOINT}"
wait_port session-a-play-route "${BINGO_SESSION_A_PLAY_ROUTE_ENDPOINT}"
start_server session-b "${SCRIPT_DIR}/Server/Session/Bingo.Server.Session.csproj" --node b
wait_port session-b-router "${BINGO_SESSION_B_ROUTER_ENDPOINT}"
wait_port session-b-stream "${BINGO_SESSION_B_STREAM_ENDPOINT}"
wait_port session-b-play-route "${BINGO_SESSION_B_PLAY_ROUTE_ENDPOINT}"

sleep "${BINGO_STARTUP_SETTLE_SECONDS:-5}"

# Give the auto-connect reconcile loops one or two polling intervals to
# converge before the scenario drives traffic.
sleep "${BINGO_STARTUP_DELAY_SECONDS:-3}"

dotnet run --no-build --project "${SCRIPT_DIR}/Client/Bingo.Client.csproj" -- \
  --stream-a-endpoint "${BINGO_SESSION_A_STREAM_ENDPOINT}" \
  --stream-b-endpoint "${BINGO_SESSION_B_STREAM_ENDPOINT}" >"${LOG_DIR}/client.log" 2>&1


# Server-side evidence is written asynchronously after the client exits;
# poll briefly instead of failing on the first read.
wait_log() {
  local pattern="$1"
  local file="$2"
  for _ in $(seq 1 50); do
    if grep -Eq "${pattern}" "${file}"; then
      return 0
    fi
    sleep 0.2
  done
  echo "Timed out waiting for '${pattern}' in ${file}" >&2
  return 1
}

grep -q "bingo=completed" "${LOG_DIR}/client.log"
grep -q "stream-inbound sample=Bingo" "${LOG_DIR}/client.log"
grep -Eq "stream-inbound sample=Bingo .* seq=[0-9]" "${LOG_DIR}/client.log"
grep -Eq "stream-inbound sample=Bingo .* name=.*Notify" "${LOG_DIR}/client.log"
wait_log "bingo observer room: actor left. observedRoom=.*observer=observer" "${LOG_DIR}/play-b.log"
wait_log "bingo room: actor left. room=.*actor=player-1" "${LOG_DIR}/play-a.log"
wait_log "bingo room: actor left. room=.*actor=player-2" "${LOG_DIR}/play-a.log"
wait_log "entry spot: actor destroy completed. actor=player-1" "${LOG_DIR}/play-a.log"
wait_log "entry spot: actor destroy completed. actor=player-2" "${LOG_DIR}/play-a.log"
require_log_count 1 "entry spot: actor left\\. actor=player-1" "${LOG_DIR}/play-a.log"
require_log_count 1 "entry spot: actor left\\. actor=player-2" "${LOG_DIR}/play-a.log"
require_log_count 1 "entry spot: actor destroy completed\\. actor=player-1" "${LOG_DIR}/play-a.log"
require_log_count 1 "entry spot: actor destroy completed\\. actor=player-2" "${LOG_DIR}/play-a.log"
require_log_count 0 "entry spot: actor destroy completed\\. actor=observer" "${LOG_DIR}/play-b.log"
grep -Rq "message flow" "${BINGO_LOG_DIR}"
