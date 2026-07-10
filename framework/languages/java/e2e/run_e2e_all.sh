#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../e2e-redis-common.sh"

REDIS_SCOPE="zlink-redis-java-e2e"
MAX_ATTEMPTS="${ZLINK_E2E_RETRY_ATTEMPTS:-3}"
SCENARIO_TIMEOUT_SECONDS="${ZLINK_JAVA_E2E_SCENARIO_TIMEOUT_SECONDS:-1800}"
BIND_RETRY_PATTERN="ZlinkBindException|BindException|Address already in use|EADDRINUSE|errno=98"

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
  SpotActorTransfer
)

cleanup_done=0

cleanup_e2e_processes() {
  local scenario_regex pattern
  scenario_regex="$(IFS='|'; echo "${DEFAULT_SCENARIOS[*]}")"
  pattern="${SCRIPT_DIR}/(${scenario_regex})/"

  pkill -TERM -f "${pattern}" >/dev/null 2>&1 || true
  sleep 0.5
  pkill -KILL -f "${pattern}" >/dev/null 2>&1 || true
}

cleanup_resources() {
  if [[ "${cleanup_done}" == "1" ]]; then
    return
  fi
  cleanup_done=1

  cleanup_e2e_processes
  zlink_redis_cleanup_scope "${REDIS_SCOPE}"
}

on_exit() {
  local code=$?
  cleanup_resources
  exit "${code}"
}

on_interrupt() {
  echo "[java-e2e] interrupted; cleaning up Java processes and Redis..." >&2
  exit 130
}

trap on_exit EXIT
trap on_interrupt INT TERM

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

zlink_redis_cleanup_scope "${REDIS_SCOPE}"

run_scenario_with_retry() {
  local scenario="$1"
  local selector="$2"
  local attempt output status started_at ended_at
  output="$(mktemp)"

  for attempt in $(seq 1 "${MAX_ATTEMPTS}"); do
    : >"${output}"
    started_at="$(date +%s)"
    set +e
    (
      cd "$SCRIPT_DIR/$scenario" &&
        nice -n 10 timeout "${SCENARIO_TIMEOUT_SECONDS}s" ./run_e2e.sh "${selector}"
    ) 2>&1 | tee "${output}"
    status="${PIPESTATUS[0]}"
    set -e
    ended_at="$(date +%s)"

    if [[ "${status}" == "0" ]]; then
      rm -f "${output}"
      echo "[java-e2e] ${scenario} PASS ($((ended_at - started_at))s)"
      return 0
    fi

    echo "[java-e2e] ${scenario} FAIL ($((ended_at - started_at))s, attempt ${attempt})" >&2
    if ! grep -Eq "${BIND_RETRY_PATTERN}" "${output}"; then
      rm -f "${output}"
      return "${status}"
    fi

    if [[ "${attempt}" == "${MAX_ATTEMPTS}" ]]; then
      rm -f "${output}"
      return "${status}"
    fi

    echo "[java-e2e] ${scenario} retry after transient bind failure (${attempt}/${MAX_ATTEMPTS})" >&2
    sleep 1
  done
}

all_started_at="$(date +%s)"
echo "[java-e2e] start configs=${#selected_scenarios[@]} at=$(date -Is)"
for index in "${!selected_scenarios[@]}"; do
  scenario="${selected_scenarios[$index]}"
  selector="${selected_selectors[$index]}"
  echo "[java-e2e] ${scenario} start scenario=${selector}"
  run_scenario_with_retry "${scenario}" "${selector}"
done
all_ended_at="$(date +%s)"

echo "[java-e2e] total PASS ($((all_ended_at - all_started_at))s)"
