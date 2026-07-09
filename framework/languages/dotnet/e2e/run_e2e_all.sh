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
  StoreFailure
  ToActorMessaging
  YieldDispatch
)

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
      echo "family=${config} scenario=${scenario} result=PASS elapsed_seconds=$((ended_at - started_at))"
      return 0
    fi

    echo "family=${config} scenario=${scenario} result=FAIL attempt=${attempt} elapsed_seconds=$((ended_at - started_at))" >&2
    if ! grep -Eq "$BIND_RETRY_PATTERN" "$output"; then
      rm -f "$output"
      return "$status"
    fi

    if [[ "$attempt" == "$MAX_ATTEMPTS" ]]; then
      rm -f "$output"
      return "$status"
    fi

    echo "e2e transient bind failure; retrying ${config}:${scenario} (${attempt}/${MAX_ATTEMPTS})" >&2
    sleep 1
  done
}

all_started_at="$(date +%s)"
echo "dotnet e2e sequential started_at=$(date -Is)"
for i in "${!SELECTED_CONFIGS[@]}"; do
  config="${SELECTED_CONFIGS[$i]}"
  scenario="${SELECTED_SCENARIOS[$i]}"
  echo "family=${config} scenario=${scenario} started_at=$(date -Is)"
  run_config_with_retry "$config" "$scenario"
done
all_ended_at="$(date +%s)"

echo "dotnet e2e sequential result=PASS total_elapsed_seconds=$((all_ended_at - all_started_at))"
