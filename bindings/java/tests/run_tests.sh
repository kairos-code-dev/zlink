#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TASKS=(
  "test"
  "integrationTest"
)

cd "${ROOT_DIR}"

failures=0
timeout_seconds=180

run_task() {
  if command -v timeout >/dev/null 2>&1; then
    timeout "${timeout_seconds}s" ./gradlew "$1" --no-daemon
    return $?
  fi
  ./gradlew "$1" --no-daemon
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
  printf 'Test summary: %d failed, %d passed\n' "$failures" \
    "$(( ${#TASKS[@]} - failures ))"
  exit 1
fi

printf 'Test summary: all %d tasks passed\n' "${#TASKS[@]}"
