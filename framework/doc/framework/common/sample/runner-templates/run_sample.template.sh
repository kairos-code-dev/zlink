#!/usr/bin/env bash
set -euo pipefail

# Individual sample runner template.
# It must not delete same-prefix Redis containers at startup.
# Expected layout:
#   samples/redis-common.sh
#   samples/<language>/<Sample>/run_sample.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/../../redis-common.sh"

LANGUAGE="${ZLINK_SAMPLE_LANGUAGE:-java}"
SAMPLE_NAME="${ZLINK_SAMPLE_NAME:-ExampleSample}"
REDIS_SCOPE="zlink-redis-${LANGUAGE}-sample"
REDIS_IMAGE="${ZLINK_REDIS_IMAGE:-redis:7-alpine}"
REDIS_READINESS_TIMEOUT_SECONDS="${ZLINK_REDIS_READY_TIMEOUT_SECONDS:-60}"
REDIS_CONTAINER_ID=""
REDIS_ENDPOINT=""
REDIS_KEY_PREFIX="${ZLINK_SAMPLE_REDIS_KEY_PREFIX:-${SAMPLE_NAME}:${LANGUAGE}:$$:${RANDOM}:}"
LOG_DIR="${SCRIPT_DIR}/logs/$(date +%Y%m%d-%H%M%S)-$$"

cleanup() {
  local status="$?"
  set +e
  # Stop only the Redis container started by this script.
  if [[ -n "${REDIS_CONTAINER_ID}" ]]; then
    docker rm -fv "${REDIS_CONTAINER_ID}" >/dev/null 2>&1 || true
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

export ZLINK_SAMPLE_REDIS_ENDPOINT="${REDIS_ENDPOINT}"
export ZLINK_SAMPLE_REDIS_KEY_PREFIX="${REDIS_KEY_PREFIX}"

# Build, start role servers, wait for readiness, then run the client self-check.
# Replace these commands with language-specific build and process commands.
#
# build_sample >"${LOG_DIR}/build.log" 2>&1
# start_servers >"${LOG_DIR}/server.log" 2>&1 &
# SERVER_PID="$!"
# wait_for_readiness
# run_client_self_check >"${LOG_DIR}/client.log" 2>&1

echo "${SAMPLE_NAME} sample result=passed"
