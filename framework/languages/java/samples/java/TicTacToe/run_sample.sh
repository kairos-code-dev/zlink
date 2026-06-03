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

gradle :Server:run --quiet --args='server' &
pids+=("$!")
wait_port 18080
wait_port 47201
wait_port 47202
wait_port 47203
wait_port 18080

gradle :Client:run --quiet
