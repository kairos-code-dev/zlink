#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 9 ]]; then
  echo "usage: $0 <registry> <api> <play> <session> <client> <work-dir> <log> <expected> <min-recv> [min-reply] [min-push] [client-log] [client-expected]" >&2
  exit 2
fi

registry_executable="$1"
api_executable="$2"
play_executable="$3"
session_executable="$4"
client_executable="$5"
work_dir="$6"
sample_log="$7"
expected_contains="$8"
min_recv="$9"
min_reply="${10:-0}"
min_push="${11:-0}"
client_log="${12:-}"
client_expected_contains="${13:-}"

mkdir -p "$work_dir"

server_pids=()
server_groups=()
lock_dir=""
cleanup() {
  local pid
  for pid in "${server_groups[@]}"; do
    kill -- "-$pid" 2>/dev/null || true
  done
  for pid in "${server_pids[@]}"; do
    if kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
    fi
  done
  for _ in $(seq 1 300); do
    local any_running="no"
    for pid in "${server_pids[@]}"; do
      if kill -0 "$pid" 2>/dev/null; then
        any_running="yes"
        break
      fi
    done
    [[ "$any_running" == "no" ]] && break
    sleep 0.01
  done
  for pid in "${server_groups[@]}"; do
    kill -9 -- "-$pid" 2>/dev/null || true
  done
  for pid in "${server_pids[@]}"; do
    if kill -0 "$pid" 2>/dev/null; then
      kill -9 "$pid" 2>/dev/null || true
    fi
  done
  for pid in "${server_pids[@]}"; do
    wait "$pid" 2>/dev/null || true
  done
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
  local bases
  if [[ "$client_executable" == *bingo* ]]; then
    bases="47101 47102 47103 47104 47110 47111 47112 47113 47114"
  elif [[ "$client_executable" == *tictactoe* ]]; then
    bases="48101 48102 48103 48104 48105 48106 48109 48110 48111 48112 48113"
  else
    bases="47101 47102 47103 47104 47110 47111 47112 47113 47114 48101 48102 48103 48104 48105 48106 48109 48110 48111 48112 48113"
  fi
  local base
  for base in $bases; do
    local port="$((base + offset))"
    if port_busy "$port"; then
      last_busy_reason="port $port is already listening"
      return 0
    fi
  done
  return 1
}

start_server() {
  local role="$1"
  local executable="$2"
  (
    cd "$work_dir"
    exec setsid env ZLINK_CPP_SAMPLE_KEEP_RUNNING=1 "$executable" \
      > "${role}.stdout" 2> "${role}.stderr"
  ) &
  server_pids+=("$!")
  server_groups+=("$!")
}

any_server_exited() {
  local pid
  for pid in "${server_pids[@]}"; do
    if ! kill -0 "$pid" 2>/dev/null; then
      return 0
    fi
  done
  return 1
}

wait_for_stream_ready() {
  local attempt
  for attempt in $(seq 1 300); do
    if [[ -f "$sample_log" ]] && grep -q "monitor stream ready" "$sample_log"; then
      return 0
    fi
    if any_server_exited; then
      return 1
    fi
    sleep 0.01
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

  server_pids=()
  server_groups=()
  rm -f "$sample_log"
  [[ -n "$client_log" ]] && rm -f "$client_log"

  start_server registry "$registry_executable"
  start_server api "$api_executable"
  start_server play "$play_executable"
  start_server session "$session_executable"

  if wait_for_stream_ready; then
    server_ready="yes"
    break
  fi

  last_server_failure="$(
    for file in registry.stderr api.stderr play.stderr session.stderr "$sample_log"; do
      [[ -f "$work_dir/$file" ]] && printf '\n== %s ==\n' "$file" && cat "$work_dir/$file"
      [[ "$file" = "$sample_log" && -f "$sample_log" ]] && printf '\n== %s ==\n' "$sample_log" && cat "$sample_log"
    done
  )"
  cleanup
done

if [[ "$server_ready" != "yes" ]]; then
  echo "sample servers exited before stream readiness" >&2
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

for _ in $(seq 1 300); do
  if [[ -f "$sample_log" ]] && grep -q "shutdown server" "$sample_log"; then
    break
  fi
  sleep 0.01
done

IFS='|' read -r -a expected_items <<< "$expected_contains"
for expected in "${expected_items[@]}"; do
  [[ -z "$expected" ]] && continue
  if ! grep -q "$expected" "$sample_log"; then
    echo "sample server log does not contain: $expected" >&2
    cat "$sample_log" >&2
    exit 1
  fi
done

recv_count=$(grep -Ec '(^| - )recv ' "$sample_log" || true)
reply_count=$(grep -Ec '(^| - )reply ' "$sample_log" || true)
push_count=$(grep -Ec '(^| - )push ' "$sample_log" || true)

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

if [[ -n "$client_log" ]]; then
  if [[ ! -f "$client_log" ]]; then
    echo "sample client log is missing: $client_log" >&2
    [[ -f "$work_dir/client.stdout" ]] && cat "$work_dir/client.stdout" >&2
    [[ -f "$work_dir/client.stderr" ]] && cat "$work_dir/client.stderr" >&2
    exit 1
  fi
  IFS='|' read -r -a client_expected_items <<< "$client_expected_contains"
  for expected in "${client_expected_items[@]}"; do
    [[ -z "$expected" ]] && continue
    if ! grep -q "$expected" "$client_log"; then
      echo "sample client log does not contain: $expected" >&2
      cat "$client_log" >&2
      exit 1
    fi
  done
fi

cleanup
trap - EXIT
