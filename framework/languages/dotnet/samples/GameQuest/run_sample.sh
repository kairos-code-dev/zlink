#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_DIR="${SAMPLE_RUN_DIR:-$(mktemp -d)}"
RUN_ID="$(basename "${RUN_DIR}")-$$-${RANDOM}"
LOG_DIR="${RUN_DIR}/logs"
SAMPLE_LOG_DIR="${RUN_DIR}/sample-logs"
export GAMEQUEST_LOG_DIR="${SAMPLE_LOG_DIR}"
mkdir -p "${LOG_DIR}" "${SAMPLE_LOG_DIR}"

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
  if [[ "${GAMEQUEST_KEEP_RUN_DIR:-}" != "1" ]]; then
    [[ -z "${SAMPLE_RUN_DIR:-}" ]] && rm -rf "${RUN_DIR}" || true
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

export GAMEQUEST_REDIS_KEY_PREFIX="gamequest:dotnet:${RUN_ID}:"
export GAMEQUEST_GAMEAPI_A_HTTP_BASE_URL="http://127.0.0.1:${PORTS[3]}"
export GAMEQUEST_GAMEAPI_B_HTTP_BASE_URL="http://127.0.0.1:${PORTS[4]}"
export GAMEQUEST_GAMEAPI_A_STREAM_ENDPOINT="ws://127.0.0.1:${PORTS[3]}/quest/ws"
export GAMEQUEST_GAMEAPI_B_STREAM_ENDPOINT="ws://127.0.0.1:${PORTS[4]}/quest/ws"
export GAMEQUEST_API_A_STREAM_BIND_ENDPOINT="tcp://127.0.0.1:${PORTS[5]}"
export GAMEQUEST_API_B_STREAM_BIND_ENDPOINT="tcp://127.0.0.1:${PORTS[6]}"
export GAMEQUEST_MISSION_A_HTTP_URL="http://127.0.0.1:${PORTS[7]}"
export GAMEQUEST_MISSION_B_HTTP_URL="http://127.0.0.1:${PORTS[8]}"
export GAMEQUEST_MISSION_A_SPOT_ENDPOINT="tcp://127.0.0.1:${PORTS[9]}"
export GAMEQUEST_MISSION_A_SPOT_ROUTER_ENDPOINT="tcp://127.0.0.1:${PORTS[10]}"
export GAMEQUEST_MISSION_B_SPOT_ENDPOINT="tcp://127.0.0.1:${PORTS[11]}"
export GAMEQUEST_MISSION_B_SPOT_ROUTER_ENDPOINT="tcp://127.0.0.1:${PORTS[12]}"

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

dotnet build "${SCRIPT_DIR}/GameQuest.csproj" --maxcpucount:1

# The sample owns its Redis: a dedicated, throwaway container is the shared
# location store every server registers into (no registry process exists).
if ! command -v docker >/dev/null 2>&1; then
  echo "Docker is required to run the GameQuest sample (it provisions a dedicated Redis container)." >&2
  exit 1
fi
REDIS_CONTAINER="zlink-gamequest-dotnet-redis-${RUN_ID}"
docker run -d --rm --name "${REDIS_CONTAINER}" -p "127.0.0.1::6379" redis:7.2-alpine >/dev/null
export GAMEQUEST_REDIS_ENDPOINT="$(docker port "${REDIS_CONTAINER}" 6379/tcp | sed -E 's/.*:([0-9]+)$/127.0.0.1:\1/')"
wait_port redis "tcp://${GAMEQUEST_REDIS_ENDPOINT}"

ASPNETCORE_URLS="${GAMEQUEST_MISSION_A_HTTP_URL}" GAMEQUEST_MISSION_NAME="mission-a" \
  start_server mission-a "${SCRIPT_DIR}/Server/QuestMission/GameQuest.QuestMission.csproj"
wait_port mission-a-spot-router "${GAMEQUEST_MISSION_A_SPOT_ROUTER_ENDPOINT}"
wait_port mission-a-spot-pub "${GAMEQUEST_MISSION_A_SPOT_ENDPOINT}"
wait_http mission-a "${GAMEQUEST_MISSION_A_HTTP_URL}"

ASPNETCORE_URLS="${GAMEQUEST_MISSION_B_HTTP_URL}" GAMEQUEST_MISSION_NAME="mission-b" \
  start_server mission-b "${SCRIPT_DIR}/Server/QuestMission/GameQuest.QuestMission.csproj"
wait_port mission-b-spot-router "${GAMEQUEST_MISSION_B_SPOT_ROUTER_ENDPOINT}"
wait_port mission-b-spot-pub "${GAMEQUEST_MISSION_B_SPOT_ENDPOINT}"
wait_http mission-b "${GAMEQUEST_MISSION_B_HTTP_URL}"

ASPNETCORE_URLS="${GAMEQUEST_GAMEAPI_A_HTTP_BASE_URL}" GAMEQUEST_API_NAME="api-a" GAMEQUEST_STREAM_BIND_ENDPOINT="${GAMEQUEST_API_A_STREAM_BIND_ENDPOINT}" \
  start_server api-a "${SCRIPT_DIR}/Server/GameApi/GameQuest.GameApi.csproj"
wait_port api-a-stream "${GAMEQUEST_API_A_STREAM_BIND_ENDPOINT}"
wait_http api-a "${GAMEQUEST_GAMEAPI_A_HTTP_BASE_URL}"

ASPNETCORE_URLS="${GAMEQUEST_GAMEAPI_B_HTTP_BASE_URL}" GAMEQUEST_API_NAME="api-b" GAMEQUEST_STREAM_BIND_ENDPOINT="${GAMEQUEST_API_B_STREAM_BIND_ENDPOINT}" \
  start_server api-b "${SCRIPT_DIR}/Server/GameApi/GameQuest.GameApi.csproj"
wait_port api-b-stream "${GAMEQUEST_API_B_STREAM_BIND_ENDPOINT}"
wait_http api-b "${GAMEQUEST_GAMEAPI_B_HTTP_BASE_URL}"

dotnet run --no-build --project "${SCRIPT_DIR}/Client/GameQuest.Client.csproj" >"${LOG_DIR}/client.log" 2>&1

grep -q "gamequest api event routed" "${LOG_DIR}/api-a.log"
grep -q "gamequest api event routed" "${LOG_DIR}/api-b.log"
grep -q "gamequest mission processed" "${LOG_DIR}/mission-a.log"
grep -q "gamequest mission processed" "${LOG_DIR}/mission-b.log"
grep -q "gamequest player quest spot ready" "${LOG_DIR}/mission-a.log"
grep -q "gamequest player quest spot ready" "${LOG_DIR}/mission-b.log"
curl -fsS -X POST "${GAMEQUEST_GAMEAPI_A_HTTP_BASE_URL}/self-check/assert" | grep -q '"passed":true'
curl -fsS "${GAMEQUEST_MISSION_A_HTTP_URL}/self-check/events" | grep -q "QuestProgressReconciledEvent"
grep -Rq "message flow" "${SAMPLE_LOG_DIR}"
echo "gamequest-server-evidence=completed"
