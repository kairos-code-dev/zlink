#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/../../runner-common.sh"
ZLINK_SAMPLE_GRADLE_SETTINGS_ARGS=(--settings-file standalone.settings.gradle.kts)

RUN_DIR="$(mktemp -d)"
RUN_ID="$(basename "${RUN_DIR}")-$$-${RANDOM}"
LOG_DIR="${RUN_DIR}/logs"
SAMPLE_LOG_DIR="${RUN_DIR}/sample-logs"
BUILD_LOG="${LOG_DIR}/build.log"
export SUPPORTCHAT_LOG_DIR="${SAMPLE_LOG_DIR}"
mkdir -p "${LOG_DIR}" "${SUPPORTCHAT_LOG_DIR}"

PIDS=()
REDIS_CONTAINER=""
export SUPPORTCHAT_REDIS_KEY_PREFIX="supportchat:kotlin:${RUN_ID}:"

on_exit() {
  local status="$?"
  if [[ "${status}" != "0" ]]; then
    for log in "${BUILD_LOG}" "${LOG_DIR}"/*.log; do
      [[ -f "${log}" ]] || continue
      echo "===== ${log} =====" >&2
      tail -n 200 "${log}" >&2 || true
    done
  fi
  cleanup
  return "${status}"
}

trap on_exit EXIT

read -r -a PORTS <<<"$(zlink_sample_reserve_ports 8)"

export SUPPORTCHAT_API_CHANNEL_ENDPOINT="tcp://127.0.0.1:${PORTS[0]}"
export SUPPORTCHAT_API_HTTP_ENDPOINT="http://127.0.0.1:${PORTS[1]}"
export SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT="tcp://127.0.0.1:${PORTS[2]}"
export SUPPORTCHAT_SESSION_SPOT_ENDPOINT="tcp://127.0.0.1:${PORTS[3]}"
export SUPPORTCHAT_SESSION_ROUTER_ENDPOINT="tcp://127.0.0.1:${PORTS[4]}"
export SUPPORTCHAT_ENTRY_SPOT_ENDPOINT="tcp://127.0.0.1:${PORTS[5]}"
export SUPPORTCHAT_ENTRY_SPOT_ROUTER_ENDPOINT="tcp://127.0.0.1:${PORTS[6]}"
export SUPPORTCHAT_STREAM_ENDPOINT="tcp://127.0.0.1:${PORTS[7]}"

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

wait_log() {
  local pattern="$1"
  local file="$2"
  for _ in $(seq 1 60); do
    if grep -Eq "${pattern}" "${file}"; then
      return 0
    fi
    sleep 0.2
  done
  echo "Timed out waiting for '${pattern}' in ${file}" >&2
  return 1
}

start_role() {
  local name="$1"
  local binary="$2"
  "${binary}" >"${LOG_DIR}/${name}.log" 2>&1 &
  PIDS+=("$!")
}

if ! command -v docker >/dev/null 2>&1; then
  echo "Docker is required to run the SupportChat sample." >&2
  exit 1
fi

zlink_redis_start_scoped_assign REDIS_CONTAINER REDIS_PORT \
  "zlink-redis-kotlin-sample-supportchat" "${ZLINK_REDIS_IMAGE:-redis:7.2-alpine}"
export SUPPORTCHAT_REDIS_ENDPOINT="127.0.0.1:${REDIS_PORT}"
wait_port redis "tcp://${SUPPORTCHAT_REDIS_ENDPOINT}"

cd "${SCRIPT_DIR}"
../../gradlew --settings-file standalone.settings.gradle.kts --no-daemon --no-parallel --max-workers=1 \
  :Server:Api:installDist \
  :Server:Session:installDist \
  :Server:Support:installDist \
  :Client:installDist >"${BUILD_LOG}" 2>&1

start_role support "${SCRIPT_DIR}/Server/Support/build/install/Support/bin/Support"
wait_port support-channel "${SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT}"
wait_port support-entry-router "${SUPPORTCHAT_ENTRY_SPOT_ROUTER_ENDPOINT}"
wait_port support-entry-pub "${SUPPORTCHAT_ENTRY_SPOT_ENDPOINT}"

start_role api "${SCRIPT_DIR}/Server/Api/build/install/Api/bin/Api"
wait_port api-channel "${SUPPORTCHAT_API_CHANNEL_ENDPOINT}"

start_role session "${SCRIPT_DIR}/Server/Session/build/install/Session/bin/Session"
wait_port session-spot "${SUPPORTCHAT_SESSION_SPOT_ENDPOINT}"
wait_port session-router "${SUPPORTCHAT_SESSION_ROUTER_ENDPOINT}"
wait_port session-stream "${SUPPORTCHAT_STREAM_ENDPOINT}"

"${SCRIPT_DIR}/Client/build/install/Client/bin/Client" \
  --stream-endpoint "${SUPPORTCHAT_STREAM_ENDPOINT}" >"${LOG_DIR}/client.log" 2>&1

grep -q "supportchat=completed" "${LOG_DIR}/client.log"
grep -q "supportchat-closed-typing-ignore=verified" "${LOG_DIR}/client.log"
wait_log "support conversation: created" "${LOG_DIR}/support.log"
wait_log "support conversation: actor joined" "${LOG_DIR}/support.log"
wait_log "status=WaitingForAgent" "${LOG_DIR}/api.log"
wait_log "status=Active" "${LOG_DIR}/support.log"
wait_log "status=WaitingForClose" "${LOG_DIR}/support.log"
wait_log "status=Closed" "${LOG_DIR}/support.log"
grep -Rq "message flow" "${SUPPORTCHAT_LOG_DIR}"
echo "supportchat-server-evidence=completed"
