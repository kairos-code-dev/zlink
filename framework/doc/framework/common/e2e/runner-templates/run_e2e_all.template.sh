#!/usr/bin/env bash
set -euo pipefail

# Aggregate e2e runner template.
# This is the only e2e script that deletes stale Redis containers by prefix.
# Expected layout:
#   e2e/redis-common.sh
#   e2e/run_e2e_all.sh
#   e2e/<Config>/run_e2e.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/redis-common.sh"

LANGUAGE="${ZLINK_E2E_LANGUAGE:-java}"
REDIS_SCOPE="zlink-redis-${LANGUAGE}-e2e"
MAX_ATTEMPTS="${ZLINK_E2E_RETRY_ATTEMPTS:-3}"
SCENARIO_TIMEOUT_SECONDS="${ZLINK_E2E_SCENARIO_TIMEOUT_SECONDS:-1800}"
BIND_RETRY_PATTERN="Address already in use|EADDRINUSE|errno=98"

CONFIGS=(
  ExampleConfigA
  ExampleConfigB
)

SELECTED_CONFIGS=()
SELECTED_SCENARIOS=()

if [[ "$#" -eq 0 ]]; then
  for config in "${CONFIGS[@]}"; do
    SELECTED_CONFIGS+=("${config}")
    SELECTED_SCENARIOS+=("all")
  done
else
  for selector in "$@"; do
    config="${selector%%:*}"
    scenario="all"
    if [[ "${selector}" == *:* ]]; then
      scenario="${selector#*:}"
      if [[ -z "${scenario}" ]]; then
        echo "Missing scenario list in selector '${selector}'." >&2
        exit 2
      fi
    fi
    SELECTED_CONFIGS+=("${config}")
    SELECTED_SCENARIOS+=("${scenario}")
  done
fi

zlink_redis_cleanup_scope "${REDIS_SCOPE}"

run_config_with_retry() {
  local config="$1"
  local scenario="$2"
  local attempt output status
  output="$(mktemp)"

  for attempt in $(seq 1 "${MAX_ATTEMPTS}"); do
    : >"${output}"
    set +e
    (
      cd "${SCRIPT_DIR}/${config}" &&
        nice -n 10 timeout "${SCENARIO_TIMEOUT_SECONDS}s" ./run_e2e.sh "${scenario}"
    ) 2>&1 | tee "${output}"
    status="${PIPESTATUS[0]}"
    set -e

    if [[ "${status}" == "0" ]]; then
      rm -f "${output}"
      return 0
    fi

    if ! grep -Eq "${BIND_RETRY_PATTERN}" "${output}"; then
      rm -f "${output}"
      return "${status}"
    fi

    if [[ "${attempt}" == "${MAX_ATTEMPTS}" ]]; then
      rm -f "${output}"
      return "${status}"
    fi

    echo "e2e transient bind failure; retrying ${config}:${scenario} (${attempt}/${MAX_ATTEMPTS})" >&2
    sleep 1
  done
}

for i in "${!SELECTED_CONFIGS[@]}"; do
  config="${SELECTED_CONFIGS[$i]}"
  scenario="${SELECTED_SCENARIOS[$i]}"
  echo "===== E2E START ${config}:${scenario} $(date +%Y-%m-%dT%H:%M:%S%z) ====="
  run_config_with_retry "${config}" "${scenario}"
  echo "===== E2E PASS ${config}:${scenario} $(date +%Y-%m-%dT%H:%M:%S%z) ====="
done

echo "e2e all result=passed"
