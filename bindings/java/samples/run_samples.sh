#!/usr/bin/env bash

set -euo pipefail

SAMPLES_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SAMPLES_DIR/.." && pwd)"
TASKS=(
  ":samples:runRequestReplyAsync"
  ":samples:runPairRecv"
  ":samples:runPubSubRecv"
  ":samples:runDealerRouterRecv"
  ":samples:runStreamRecv"
  ":samples:runStreamPacketCallback"
  ":samples:runMonitorRecv"
  ":samples:runSpotRecv"
  ":samples:runSpotRequestAsync"
)

failures=0
timeout_seconds=240

run_task() {
  if command -v timeout >/dev/null 2>&1; then
    timeout "${timeout_seconds}s" "$ROOT_DIR/gradlew" -p "$ROOT_DIR" "$1" --no-daemon
    return $?
  fi
  "$ROOT_DIR/gradlew" -p "$ROOT_DIR" "$1" --no-daemon
}

for task in "${TASKS[@]}"; do
  printf '[RUN] %s\n' "$task"
  if ! run_task "$task"; then
    printf '[FAIL] %s\n' "$task"
    failures=$((failures + 1))
  else
    printf '[PASS] %s\n' "$task"
  fi
done

if (( failures > 0 )); then
  printf 'Sample summary: %d failed, %d passed\n' "$failures" \
    "$(( ${#TASKS[@]} - failures ))"
  exit 1
fi

printf 'Sample summary: all %d tasks passed\n' "${#TASKS[@]}"
