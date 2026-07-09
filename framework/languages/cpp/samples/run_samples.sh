#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/redis-common.sh"

REDIS_SCOPE="zlink-redis-cpp-sample"
MAX_ATTEMPTS="${ZLINK_SAMPLE_RETRY_ATTEMPTS:-3}"

SAMPLE_RUNNERS=(
  TicTacToe/run_sample.sh
  Bingo/run_sample.sh
  DeliveryDispatch/run_sample.sh
  SupportChat/run_sample.sh
  GameQuest/run_sample.sh
  ShoppingMall/run_sample.sh
)

zlink_redis_cleanup_scope "$REDIS_SCOPE"

run_sample_with_retry() {
  local runner="$1"
  local attempt output status
  output="$(mktemp)"

  for attempt in $(seq 1 "$MAX_ATTEMPTS"); do
    : >"$output"
    set +e
    "$SCRIPT_DIR/$runner" 2>&1 | tee "$output"
    status="${PIPESTATUS[0]}"
    set -e

    if [[ "$status" == "0" ]]; then
      rm -f "$output"
      return 0
    fi

    if ! grep -Eq "ZlinkBindException|BindException|Address already in use" "$output"; then
      rm -f "$output"
      return "$status"
    fi

    if [[ "$attempt" == "$MAX_ATTEMPTS" ]]; then
      rm -f "$output"
      return "$status"
    fi

    echo "sample transient bind failure; retrying ${runner} (${attempt}/${MAX_ATTEMPTS})" >&2
    sleep 1
  done
}

for runner in "${SAMPLE_RUNNERS[@]}"; do
  run_sample_with_retry "$runner"
done

echo "sample all result=passed"
