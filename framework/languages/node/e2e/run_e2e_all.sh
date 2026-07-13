#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MAX_ATTEMPTS="${ZLINK_E2E_RETRY_ATTEMPTS:-3}"
SCENARIO_TIMEOUT_SECONDS="${ZLINK_NODE_E2E_SCENARIO_TIMEOUT_SECONDS:-1800}"
DEFAULT_CONFIGS=(
  DiscoveryRegistryHa
  RegistrationCodec
  RegistryMessaging
  PubSub
  SpotService
  RuntimeMonitoring
  ResilienceLifecycle
  AutomaticTurnDispatch
  ObservabilityOps
  ToActorMessaging
  SpotActorTransfer
)
BIND_RETRY_PATTERN="ZlinkBindException|BindException|Address already in use|EADDRINUSE|errno=98"

cleanup_done=0
active_config_pid=""

cleanup_resources() {
  if [[ "${cleanup_done}" == "1" ]]; then
    return
  fi
  cleanup_done=1

  if [[ -n "${active_config_pid}" ]] && kill -0 "${active_config_pid}" >/dev/null 2>&1; then
    kill -TERM "${active_config_pid}" >/dev/null 2>&1 || true
    wait "${active_config_pid}" >/dev/null 2>&1 || true
  fi
}

on_exit() {
  local code=$?
  cleanup_resources
  exit "${code}"
}

on_interrupt() {
  echo "[node-e2e] interrupted; stopping the current Node.js configuration..." >&2
  exit 130
}

trap on_exit EXIT
trap on_interrupt INT TERM

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
  local attempt output status started_at ended_at
  output="$(mktemp)"

  for attempt in $(seq 1 "${MAX_ATTEMPTS}"); do
    : >"${output}"
    started_at="$(date +%s)"
    set +e
    (
      cd "${SCRIPT_DIR}/${config}" &&
        exec nice -n 10 timeout "${SCENARIO_TIMEOUT_SECONDS}s" ./run_e2e.sh "${scenario}"
    ) > >(tee "${output}") 2>&1 &
    active_config_pid="$!"
    wait "${active_config_pid}"
    status="$?"
    active_config_pid=""
    set -e
    ended_at="$(date +%s)"

    if [[ "${status}" == "0" ]]; then
      rm -f "${output}"
      echo "[node-e2e] ${config} PASS ($((ended_at - started_at))s)"
      return 0
    fi

    echo "[node-e2e] ${config} FAIL ($((ended_at - started_at))s, attempt ${attempt})" >&2
    if ! grep -Eq "${BIND_RETRY_PATTERN}" "${output}"; then
      rm -f "${output}"
      return "${status}"
    fi

    if [[ "${attempt}" == "${MAX_ATTEMPTS}" ]]; then
      rm -f "${output}"
      return "${status}"
    fi

    echo "[node-e2e] ${config} retry after transient bind failure (${attempt}/${MAX_ATTEMPTS})" >&2
    sleep 1
  done
}

all_started_at="$(date +%s)"
echo "[node-e2e] start configs=${#selected_configs[@]} at=$(date -Is)"
for index in "${!selected_configs[@]}"; do
  config="${selected_configs[$index]}"
  scenario="${selected_scenarios[$index]}"
  echo "[node-e2e] ${config} start scenario=${scenario}"
  run_config_with_retry "${config}" "${scenario}"
done
all_ended_at="$(date +%s)"

echo "[node-e2e] total PASS ($((all_ended_at - all_started_at))s)"
