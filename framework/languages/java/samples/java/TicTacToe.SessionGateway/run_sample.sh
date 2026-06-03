#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

pids=()
cleanup() {
  for pid in "${pids[@]}"; do
    kill "$pid" >/dev/null 2>&1 || true
  done
}
trap cleanup EXIT

wait_port() {
  local port="$1"
  local deadline=$((SECONDS + 15))
  while (( SECONDS < deadline )); do
    if (echo >"/dev/tcp/127.0.0.1/$port") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for port $port" >&2
  return 1
}

gradle :Server:Registry:run --quiet &
pids+=("$!")
wait_port 19182
gradle :Server:Api:run --quiet &
pids+=("$!")
wait_port 47403
gradle :Server:Play:run --quiet &
pids+=("$!")
wait_port 47404
gradle :Server:Session:run --quiet &
pids+=("$!")
wait_port 47412

gradle :Client:run --quiet
