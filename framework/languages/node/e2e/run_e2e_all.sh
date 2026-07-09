#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/redis-container.sh"

SCENARIO_TIMEOUT_SECONDS="${ZLINK_NODE_E2E_SCENARIO_TIMEOUT_SECONDS:-1800}"
DEFAULT_CONFIGS=(
  DiscoveryRegistryHa
  RegistrationCodec
  RegistryMessaging
  PubSub
  SpotService
  RuntimeMonitoring
  ResilienceLifecycle
  YieldDispatch
  ToActorMessaging
)
BIND_RETRY_PATTERN="ZlinkBindException|BindException|Address already in use|EADDRINUSE|errno=98"

zlink_redis_cleanup_scope "zlink-redis-node-e2e"

selected_configs=()
selected_scenarios=()

if [[ "$#" -eq 0 ]]; then
  for config in "${DEFAULT_CONFIGS[@]}"; do
    selected_configs+=("${config}")
    selected_scenarios+=("all")
  done
else
  for item in "$@"; do
    if [[ "${item}" == *:* ]]; then
      selected_configs+=("${item%%:*}")
      selected_scenarios+=("${item#*:}")
    else
      selected_configs+=("${item}")
      selected_scenarios+=("all")
    fi
  done
fi

run_config_with_retry() {
  local config="$1"
  local scenario="$2"
  local attempt output status
  output="$(mktemp)"
  for attempt in 1 2 3; do
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
    if [[ "${attempt}" == "3" ]]; then
      rm -f "${output}"
      return "${status}"
    fi
    echo "node e2e transient bind failure; retrying ${config}:${scenario} (${attempt}/3)" >&2
    sleep 1
  done
}

for index in "${!selected_configs[@]}"; do
  config="${selected_configs[$index]}"
  scenario="${selected_scenarios[$index]}"
  echo "===== NODE E2E START ${config}:${scenario} $(date +%Y-%m-%dT%H:%M:%S%z) ====="
  run_config_with_retry "${config}" "${scenario}"
  echo "===== NODE E2E PASS ${config}:${scenario} $(date +%Y-%m-%dT%H:%M:%S%z) ====="
done

echo "node e2e all result=passed"
