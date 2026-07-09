#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/redis-common.sh"

REDIS_SCOPE="zlink-redis-cpp-e2e"
MAX_ATTEMPTS="${ZLINK_E2E_RETRY_ATTEMPTS:-3}"
SCENARIO_TIMEOUT_SECONDS="${ZLINK_E2E_SCENARIO_TIMEOUT_SECONDS:-1800}"

CONFIGS=(
  RegistrationCodec
  RegistryMessaging
  SpotService
  PubSub
  ResilienceLifecycle
  DiscoveryRegistryHa
  DeliveryDispatch
  RuntimeMonitoring
  YieldDispatch
  ToActorMessaging
)

zlink_redis_cleanup_scope "${REDIS_SCOPE}"

run_config_with_retry() {
  local config="$1"
  local attempt output status
  output="$(mktemp)"

  for attempt in $(seq 1 "${MAX_ATTEMPTS}"); do
    : >"${output}"
    set +e
    (
      cd "${SCRIPT_DIR}/${config}" &&
        timeout "${SCENARIO_TIMEOUT_SECONDS}s" ./run_e2e.sh all
    ) 2>&1 | tee "${output}"
    status="${PIPESTATUS[0]}"
    set -e

    if [[ "${status}" == "0" ]]; then
      rm -f "${output}"
      return 0
    fi

    if ! grep -Eq "ZlinkBindException|BindException|Address already in use" "${output}"; then
      rm -f "${output}"
      return "${status}"
    fi

    if [[ "${attempt}" == "${MAX_ATTEMPTS}" ]]; then
      rm -f "${output}"
      return "${status}"
    fi

    echo "e2e transient bind failure; retrying ${config} (${attempt}/${MAX_ATTEMPTS})" >&2
    sleep 1
  done
}

for config in "${CONFIGS[@]}"; do
  echo "===== E2E START ${config} $(date +%Y-%m-%dT%H:%M:%S%z) ====="
  run_config_with_retry "${config}"
  echo "===== E2E PASS ${config} $(date +%Y-%m-%dT%H:%M:%S%z) ====="
done

echo "e2e all result=passed"
