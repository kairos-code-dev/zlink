#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/redis-common.sh"

REDIS_SCOPE="zlink-redis-dotnet-e2e"
MAX_ATTEMPTS="${ZLINK_E2E_RETRY_ATTEMPTS:-3}"
CONFIG_TIMEOUT_SECONDS="${ZLINK_E2E_CONFIG_TIMEOUT_SECONDS:-1800}"
BIND_RETRY_PATTERN="ZlinkBindException|BindException|Address already in use|EADDRINUSE"

CONFIGS=(
  LocationMessaging
  PubSub
  RegistrationCodec
  ResilienceLifecycle
  RuntimeMonitoring
  SpotService
  SpotActorTransfer
  StoreFailure
  ToActorMessaging
  AutomaticTurnDispatch
)

cleanup_done=0

cleanup_e2e_processes() {
  local pattern config_regex
  config_regex="$(IFS='|'; echo "${CONFIGS[*]}")"
  pattern="${SCRIPT_DIR}/(${config_regex})/"

  pkill -TERM -f "$pattern" >/dev/null 2>&1 || true
  sleep 0.5
  pkill -KILL -f "$pattern" >/dev/null 2>&1 || true
}

cleanup_resources() {
  if [[ "$cleanup_done" == "1" ]]; then
    return
  fi
  cleanup_done=1

  cleanup_e2e_processes
  zlink_redis_cleanup_scope "$REDIS_SCOPE"
}

on_exit() {
  local code=$?
  cleanup_resources
  exit "$code"
}

on_interrupt() {
  echo "[dotnet-e2e] interrupted; cleaning up dotnet processes and Redis..." >&2
  exit 130
}

trap on_exit EXIT
trap on_interrupt INT TERM

SELECTED_CONFIGS=()
SELECTED_SCENARIOS=()

if [[ "$#" -eq 0 ]]; then
  for config in "${CONFIGS[@]}"; do
    SELECTED_CONFIGS+=("$config")
    SELECTED_SCENARIOS+=("all")
  done
else
  for selector in "$@"; do
    config="${selector%%:*}"
    scenario="all"
    if [[ "$selector" == *:* ]]; then
      scenario="${selector#*:}"
      if [[ -z "$scenario" ]]; then
        echo "Missing scenario list in selector '$selector'." >&2
        exit 2
      fi
    fi

    known=0
    for candidate in "${CONFIGS[@]}"; do
      if [[ "$candidate" == "$config" ]]; then
        known=1
        break
      fi
    done
    if [[ "$known" == "0" ]]; then
      echo "Unknown e2e config '$config'." >&2
      exit 2
    fi

    SELECTED_CONFIGS+=("$config")
    SELECTED_SCENARIOS+=("$scenario")
  done
fi

zlink_redis_cleanup_scope "$REDIS_SCOPE"

run_config_with_retry() {
  local config="$1"
  local scenario="$2"
  local attempt output status started_at ended_at
  output="$(mktemp)"

  for attempt in $(seq 1 "$MAX_ATTEMPTS"); do
    : >"$output"
    started_at="$(date +%s)"
    set +e
    (
      cd "$SCRIPT_DIR/$config" &&
        nice -n 10 timeout "${CONFIG_TIMEOUT_SECONDS}s" ./run_e2e.sh "$scenario"
    ) 2>&1 | tee "$output"
    status="${PIPESTATUS[0]}"
    set -e
    ended_at="$(date +%s)"

    if [[ "$status" == "0" ]]; then
      rm -f "$output"
      echo "[dotnet-e2e] ${config} PASS ($((ended_at - started_at))s)"
      return 0
    fi

    echo "[dotnet-e2e] ${config} FAIL ($((ended_at - started_at))s, attempt ${attempt})" >&2
    if ! grep -Eq "$BIND_RETRY_PATTERN" "$output"; then
      rm -f "$output"
      return "$status"
    fi

    if [[ "$attempt" == "$MAX_ATTEMPTS" ]]; then
      rm -f "$output"
      return "$status"
    fi

    echo "[dotnet-e2e] ${config} retry after transient bind failure (${attempt}/${MAX_ATTEMPTS})" >&2
    sleep 1
  done
}

all_started_at="$(date +%s)"
echo "[dotnet-e2e] start configs=${#SELECTED_CONFIGS[@]} at=$(date -Is)"
for i in "${!SELECTED_CONFIGS[@]}"; do
  config="${SELECTED_CONFIGS[$i]}"
  scenario="${SELECTED_SCENARIOS[$i]}"
  echo "[dotnet-e2e] ${config} start scenario=${scenario}"
  run_config_with_retry "$config" "$scenario"
done
all_ended_at="$(date +%s)"

echo "[dotnet-e2e] total PASS ($((all_ended_at - all_started_at))s)"
