#!/usr/bin/env bash
set -euo pipefail

# Individual e2e config runner template.
# It must not delete same-prefix Redis containers at startup.
# Expected layout:
#   e2e/redis-common.sh
#   e2e/<Config>/run_e2e.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/../redis-common.sh"

LANGUAGE="${ZLINK_E2E_LANGUAGE:-java}"
CONFIG_NAME="${ZLINK_E2E_CONFIG_NAME:-ExampleConfig}"
if [[ "$#" -eq 0 ]]; then
  SCENARIO="all"
else
  SCENARIO="$*"
  SCENARIO="${SCENARIO// /,}"
fi
REDIS_SCOPE="zlink-redis-${LANGUAGE}-e2e"
REDIS_IMAGE="${ZLINK_REDIS_IMAGE:-redis:7-alpine}"
REDIS_READINESS_TIMEOUT_SECONDS="${ZLINK_REDIS_READY_TIMEOUT_SECONDS:-60}"
REDIS_CONTAINER_ID=""
REDIS_ENDPOINT=""
REDIS_KEY_PREFIX="${ZLINK_E2E_REDIS_KEY_PREFIX:-${CONFIG_NAME}:${LANGUAGE}:$$:${RANDOM}:}"
LOG_DIR="${SCRIPT_DIR}/logs/$(date +%Y%m%d-%H%M%S)-$$"

cleanup() {
  local status="$?"
  set +e
  # Stop only the Redis container started by this script.
  if [[ -n "${REDIS_CONTAINER_ID}" ]]; then
    docker rm -f "${REDIS_CONTAINER_ID}" >/dev/null 2>&1 || true
  fi
  # Stop only server/client processes started by this script.
  # kill "${SERVER_PID}" >/dev/null 2>&1 || true
  return "${status}"
}
trap cleanup EXIT

mkdir -p "${LOG_DIR}"
echo "log_dir=${LOG_DIR}"

zlink_redis_start_scoped_assign \
  REDIS_CONTAINER_ID \
  redis_host_port \
  "${REDIS_SCOPE}" \
  "${REDIS_IMAGE}" \
  "127.0.0.1::6379"
REDIS_ENDPOINT="127.0.0.1:${redis_host_port}"
zlink_redis_wait_ready "${REDIS_CONTAINER_ID}" "${REDIS_READINESS_TIMEOUT_SECONDS}"

export ZLINK_E2E_REDIS_ENDPOINT="${REDIS_ENDPOINT}"
export ZLINK_E2E_REDIS_KEY_PREFIX="${REDIS_KEY_PREFIX}"
export ZLINK_E2E_SCENARIO="${SCENARIO}"

# Build, start role servers, wait for readiness, then run the selected scenario.
# Replace these commands with language-specific build and process commands.
#
# build_e2e >"${LOG_DIR}/build.log" 2>&1
# start_role_servers >"${LOG_DIR}/servers.log" 2>&1 &
# SERVER_PID="$!"
# wait_for_readiness
# run_client_scenario "${SCENARIO}" >"${LOG_DIR}/client.log" 2>&1

echo "${CONFIG_NAME} e2e result=passed"
