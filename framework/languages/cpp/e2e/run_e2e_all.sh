#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUNNER_CWD="$(pwd)"

MAX_ATTEMPTS="${ZLINK_E2E_RETRY_ATTEMPTS:-3}"
SCENARIO_TIMEOUT_SECONDS="${ZLINK_E2E_SCENARIO_TIMEOUT_SECONDS:-1800}"
BIND_RETRY_PATTERN="Address already in use|EADDRINUSE|errno=98"

if [[ -n "${ZLINK_CPP_E2E_BUILD_DIR:-}" && "${ZLINK_CPP_E2E_BUILD_DIR}" != /* ]]; then
  export ZLINK_CPP_E2E_BUILD_DIR="${RUNNER_CWD}/${ZLINK_CPP_E2E_BUILD_DIR}"
fi

CONFIGS=(
  RegistrationCodec
  RegistryMessaging
  SpotService
  PubSub
  ResilienceLifecycle
  DiscoveryRegistryHa
  RuntimeMonitoring
  AutomaticTurnDispatch
  ToActorMessaging
  SpotActorTransfer
  ObservabilityOps
)

START_ORDER_VARIANTS=(
  forward
  reverse
  shuffle:20260709
)

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
  echo "[cpp-e2e] interrupted; stopping the current configuration..." >&2
  exit 130
}

trap on_exit EXIT
trap on_interrupt INT TERM

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
    if [[ "$selector" == *:* ]]; then
      scenario="${selector#*:}"
      if [[ -z "$scenario" ]]; then
        echo "Missing scenario list in selector '${selector}'." >&2
        exit 2
      fi
    fi

    matched=0
    for known_config in "${CONFIGS[@]}"; do
      if [[ "$config" == "$known_config" ]]; then
        matched=1
        break
      fi
    done
    if [[ "$matched" == "0" ]]; then
      echo "Unknown e2e config selector '${config}'." >&2
      exit 2
    fi

    IFS=',' read -ra scenario_items <<<"${scenario}"
    for scenario_item in "${scenario_items[@]}"; do
      if [[ -z "${scenario_item}" ]]; then
        echo "Empty scenario in selector '${selector}'." >&2
        exit 2
      fi
      SELECTED_CONFIGS+=("${config}")
      SELECTED_SCENARIOS+=("${scenario_item}")
    done
  done
fi

run_config_with_retry() {
  local config="$1"
  local scenario="$2"
  local start_order="$3"
  local attempt output status started_at ended_at
  output="$(mktemp)"

  for attempt in $(seq 1 "${MAX_ATTEMPTS}"); do
    : >"${output}"
    started_at="$(date +%s)"
    set +e
    (
      cd "${SCRIPT_DIR}/${config}" &&
        exec env E2E_START_ORDER="${start_order}" timeout "${SCENARIO_TIMEOUT_SECONDS}s" ./run_e2e.sh "${scenario}"
    ) > >(tee "${output}") 2>&1 &
    active_config_pid="$!"
    wait "${active_config_pid}"
    status="$?"
    active_config_pid=""
    set -e
    ended_at="$(date +%s)"

    if [[ "${status}" == "0" ]]; then
      rm -f "${output}"
      echo "[cpp-e2e] ${config} PASS ($((ended_at - started_at))s, start_order=${start_order})"
      return 0
    fi

    echo "[cpp-e2e] ${config} FAIL ($((ended_at - started_at))s, attempt ${attempt}, start_order=${start_order})" >&2
    if ! grep -Eq "${BIND_RETRY_PATTERN}" "${output}"; then
      rm -f "${output}"
      return "${status}"
    fi

    if [[ "${attempt}" == "${MAX_ATTEMPTS}" ]]; then
      rm -f "${output}"
      return "${status}"
    fi

    echo "[cpp-e2e] ${config} retry after transient bind failure (${attempt}/${MAX_ATTEMPTS}, start_order=${start_order})" >&2
    sleep 1
  done
}

all_started_at="$(date +%s)"
echo "[cpp-e2e] start configs=${#SELECTED_CONFIGS[@]} start_orders=${#START_ORDER_VARIANTS[@]} at=$(date -Is)"
for i in "${!SELECTED_CONFIGS[@]}"; do
  config="${SELECTED_CONFIGS[$i]}"
  scenario="${SELECTED_SCENARIOS[$i]}"
  for start_order in "${START_ORDER_VARIANTS[@]}"; do
    echo "[cpp-e2e] ${config} start scenario=${scenario} start_order=${start_order}"
    run_config_with_retry "${config}" "${scenario}" "${start_order}"
  done
done
all_ended_at="$(date +%s)"

echo "[cpp-e2e] total PASS ($((all_ended_at - all_started_at))s)"
