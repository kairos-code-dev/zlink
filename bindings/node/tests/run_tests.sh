#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

run_node_job() {
  local timeout_sec="$1"
  shift
  setsid "$@" &
  local job_pid=$!
  local watcher_pid=""
  local rc=0

  (
    sleep "$timeout_sec"
    if kill -0 "$job_pid" 2>/dev/null; then
      kill -TERM -- "-$job_pid" 2>/dev/null || true
      sleep 2
      kill -KILL -- "-$job_pid" 2>/dev/null || true
    fi
  ) &
  watcher_pid=$!

  if ! wait "$job_pid"; then
    rc=$?
  fi

  kill "$watcher_pid" 2>/dev/null || true
  wait "$watcher_pid" 2>/dev/null || true
  kill -TERM -- "-$job_pid" 2>/dev/null || true

  return "$rc"
}

for test_file in dist-tools/tests/*.test.js; do
  printf '[test] %s\n' "$test_file"
  run_node_job 120 node --test "$test_file"
done
