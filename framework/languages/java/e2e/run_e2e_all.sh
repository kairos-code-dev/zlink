#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../e2e-redis-common.sh"
BIND_RETRY_PATTERN="ZlinkBindException|BindException|Address already in use|EADDRINUSE|errno=98"

zlink_redis_cleanup_scope "zlink-redis-java-e2e"

DEFAULT_SCENARIOS=(
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

selected_scenarios=()
selected_selectors=()

if [[ "$#" -eq 0 ]]; then
  for scenario in "${DEFAULT_SCENARIOS[@]}"; do
    selected_scenarios+=("${scenario}")
    selected_selectors+=("all")
  done
else
  for selector in "$@"; do
    if [[ "${selector}" == *:* ]]; then
      selected_scenarios+=("${selector%%:*}")
      selected_selectors+=("${selector#*:}")
    else
      selected_scenarios+=("${selector}")
      selected_selectors+=("all")
    fi
  done
fi

run_scenario_with_retry() {
  local scenario="$1"
  local selector="$2"
  local attempt
  local output
  output="$(mktemp)"
  for attempt in 1 2 3; do
    : >"${output}"
    set +e
    (
      cd "$SCRIPT_DIR/$scenario" &&
        nice -n 10 timeout "${ZLINK_JAVA_E2E_SCENARIO_TIMEOUT_SECONDS:-1800}s" ./run_e2e.sh "${selector}"
    ) 2>&1 | tee "${output}"
    local status="${PIPESTATUS[0]}"
    set -e
    if [[ "${status}" == "0" ]]; then
      rm -f "${output}"
      return 0
    fi
    if ! rg -q "${BIND_RETRY_PATTERN}" "${output}"; then
      rm -f "${output}"
      return "${status}"
    fi
    if [[ "${attempt}" == "3" ]]; then
      rm -f "${output}"
      return "${status}"
    fi
    echo "java e2e transient bind failure; retrying ${scenario}:${selector} (${attempt}/3)" >&2
    sleep 1
  done
}

for index in "${!selected_scenarios[@]}"; do
  scenario="${selected_scenarios[$index]}"
  selector="${selected_selectors[$index]}"
  echo "===== JAVA E2E START ${scenario}:${selector} $(date +%Y-%m-%dT%H:%M:%S%z) ====="
  run_scenario_with_retry "${scenario}" "${selector}"
  echo "===== JAVA E2E PASS ${scenario}:${selector} $(date +%Y-%m-%dT%H:%M:%S%z) ====="
done

echo "java e2e all result=passed"
