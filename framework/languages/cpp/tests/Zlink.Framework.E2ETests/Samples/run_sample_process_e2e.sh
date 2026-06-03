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
rm -f "$sample_log"

server_pid=""
cleanup() {
  if [[ -n "$server_pid" ]] && kill -0 "$server_pid" 2>/dev/null; then
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
  fi
}
trap cleanup EXIT

(
  cd "$work_dir"
  "$server_executable"
) &
server_pid="$!"

for _ in $(seq 1 200); do
  if [[ -f "$sample_log" ]] && grep -q "monitor stream ready" "$sample_log"; then
    break
  fi
  if ! kill -0 "$server_pid" 2>/dev/null; then
    wait "$server_pid" || true
    echo "sample server exited before readiness" >&2
    [[ -f "$sample_log" ]] && cat "$sample_log" >&2
    exit 1
  fi
  sleep 0.01
done

if [[ ! -f "$sample_log" ]] || ! grep -q "monitor stream ready" "$sample_log"; then
  echo "sample server did not report readiness" >&2
  [[ -f "$sample_log" ]] && cat "$sample_log" >&2
  exit 1
fi

(
  cd "$work_dir"
  ZLINK_SAMPLE_EXTERNAL_SERVER=1 "$client_executable" \
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
