#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 6 ]]; then
  echo "usage: $0 <server> <client> <work-dir> <log> <expected> <min-recv> [min-reply] [min-push]" >&2
  exit 2
fi

server_executable="$1"
client_executable="$2"
work_dir="$3"
sample_log="$4"
expected_contains="$5"
min_recv="$6"
min_reply="${7:-0}"
min_push="${8:-0}"

mkdir -p "$work_dir"

server_pid=""
lock_dir=""
cleanup() {
  if [[ -n "$server_pid" ]] && kill -0 "$server_pid" 2>/dev/null; then
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
  fi
  if [[ -n "$lock_dir" ]] && [[ -d "$lock_dir" ]]; then
    rmdir "$lock_dir" 2>/dev/null || true
  fi
}
trap cleanup EXIT

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
lock_key="$(printf '%s' "$script_dir" | cksum | awk '{print $1}')"
lock_root="${TMPDIR:-/tmp}/zlink-sample-process-e2e-${lock_key}"
mkdir -p "$lock_root"
if command -v flock >/dev/null 2>&1; then
  exec 9>"$lock_root/.sample-process-e2e.lock"
  flock 9
else
  lock_dir="$lock_root/.sample-process-e2e.lockdir"
  while ! mkdir "$lock_dir" 2>/dev/null; do
    sleep 0.01
  done
fi

port_busy() {
  local port="$1"
  if ! command -v ss >/dev/null 2>&1; then
    return 1
  fi
  ss -ltn "sport = :$port" | tail -n +2 | grep -q .
}

last_busy_reason=""
sample_offset_busy() {
  local offset="$1"
  local base
  local bases
  if [[ "$server_executable" == *bingo* ]]; then
    bases="47101 47102 47103 47104 47110 47111 47112 47113 47114"
  elif [[ "$server_executable" == *tictactoe* ]]; then
    bases="48101 48102 48103 48104 48105 48106 48109 48110 48111 48112 48113"
  else
    bases="47101 47102 47103 47104 47110 47111 47112 47113 47114 48101 48102 48103 48104 48105 48106 48109 48110 48111 48112 48113"
  fi
  for base in $bases; do
    local port="$((base + offset))"
    if port_busy "$port"; then
      last_busy_reason="port $port is already listening"
      return 0
    fi
  done
  return 1
}

server_ready="no"
last_server_failure=""
skipped_offsets=0
for attempt in $(seq 0 79); do
  port_offset=$((15000 + ((lock_key + $$ + attempt * 97) % 2300)))
  if sample_offset_busy "$port_offset"; then
    skipped_offsets=$((skipped_offsets + 1))
    continue
  fi
  export ZLINK_CPP_SAMPLE_PORT_OFFSET="$port_offset"

  rm -f "$sample_log"

  (
    cd "$work_dir"
    "$server_executable"
  ) &
  server_pid="$!"

  for _ in $(seq 1 200); do
    if [[ -f "$sample_log" ]] && grep -q "monitor stream ready" "$sample_log"; then
      server_ready="yes"
      break
    fi
    if ! kill -0 "$server_pid" 2>/dev/null; then
      wait "$server_pid" || true
      last_server_failure="$(cat "$sample_log" 2>/dev/null || true)"
      server_pid=""
      break
    fi
    sleep 0.01
  done

  if [[ "$server_ready" == "yes" ]]; then
    break
  fi
  if [[ -n "$server_pid" ]] && kill -0 "$server_pid" 2>/dev/null; then
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
    server_pid=""
  fi
done

if [[ "$server_ready" != "yes" ]]; then
  echo "sample server exited before readiness" >&2
  if (( skipped_offsets > 0 )); then
    echo "skipped $skipped_offsets port offsets; last skip: $last_busy_reason" >&2
  fi
  [[ -n "$last_server_failure" ]] && printf '%s\n' "$last_server_failure" >&2
  exit 1
fi

(
  cd "$work_dir"
  "$client_executable" \
    > client.stdout 2> client.stderr
) || {
  client_status=$?
  echo "sample client failed with $client_status" >&2
  [[ -f "$work_dir/client.stdout" ]] && cat "$work_dir/client.stdout" >&2
  [[ -f "$work_dir/client.stderr" ]] && cat "$work_dir/client.stderr" >&2
  [[ -f "$sample_log" ]] && cat "$sample_log" >&2
  exit "$client_status"
}

wait "$server_pid"
server_pid=""

IFS='|' read -r -a expected_items <<< "$expected_contains"
for expected in "${expected_items[@]}"; do
  [[ -z "$expected" ]] && continue
  if ! grep -q "$expected" "$sample_log"; then
    echo "sample server log does not contain: $expected" >&2
    cat "$sample_log" >&2
    exit 1
  fi
done

recv_count=$(grep -c '^recv ' "$sample_log" || true)
reply_count=$(grep -c '^reply ' "$sample_log" || true)
push_count=$(grep -c '^push ' "$sample_log" || true)

if (( recv_count < min_recv )); then
  echo "expected at least $min_recv recv lines, got $recv_count" >&2
  cat "$sample_log" >&2
  exit 1
fi
if (( reply_count < min_reply )); then
  echo "expected at least $min_reply reply lines, got $reply_count" >&2
  cat "$sample_log" >&2
  exit 1
fi
if (( push_count < min_push )); then
  echo "expected at least $min_push push lines, got $push_count" >&2
  cat "$sample_log" >&2
  exit 1
fi
