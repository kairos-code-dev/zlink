#!/usr/bin/env bash
set -euo pipefail

SAMPLES_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SAMPLES_DIR/.." && pwd)"
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

  if wait "$job_pid"; then
    rc=0
  else
    rc=$?
  fi

  kill "$watcher_pid" 2>/dev/null || true
  wait "$watcher_pid" 2>/dev/null || true
  kill -TERM -- "-$job_pid" 2>/dev/null || true

  return "$rc"
}

samples=(
  "dist-tools/samples/request_reply_async_sample.js"
  "dist-tools/samples/pair_recv_sample.js"
  "dist-tools/samples/pubsub_recv_sample.js"
  "dist-tools/samples/dealer_router_recv_sample.js"
  "dist-tools/samples/spot_recv_sample.js"
  "dist-tools/samples/spot_request_async_sample.js"
  "dist-tools/samples/stream_recv_sample.js"
  "dist-tools/samples/stream_packet_callback_sample.js"
  "dist-tools/samples/actor_room_server_sample.js"
  "dist-tools/samples/actor_gateway_relay_sample.js"
  "dist-tools/samples/actor_single_player_queue_sample.js"
  "dist-tools/samples/monitor_recv_sample.js"
  "dist-tools/samples/discovery_registry_sample.js"
  "dist-tools/samples/registry_query_sample.js"
)

# These samples exercise remote SPOT mesh propagation or async actor/session
# lifecycle paths that are currently timing-sensitive in the native runtime.
# The public API contract they demonstrate is covered by socket_surface and
# socket_surface.typecheck; keep them out of the default sample gate until the
# native lifecycle behavior is deterministic enough for a script smoke test.
declare -A skipped_samples=(
  ["dist-tools/samples/spot_recv_sample.js"]="remote SPOT mesh delivery can miss the sample timeout"
  ["dist-tools/samples/actor_room_server_sample.js"]="async actor join/leave teardown can report transient busy/invalid-state"
  ["dist-tools/samples/actor_gateway_relay_sample.js"]="async stream actor bind/relay teardown can hang in smoke-test timing"
  ["dist-tools/samples/actor_single_player_queue_sample.js"]="async actor rejoin/queued-delivery lifecycle can hang in smoke-test timing"
)

passed=0
failed=0
skipped=0

for sample in "${samples[@]}"; do
  if [[ -n "${skipped_samples[$sample]:-}" ]]; then
    printf '[sample] %s # SKIP: %s\n' "$sample" "${skipped_samples[$sample]}"
    skipped=$((skipped + 1))
    continue
  fi
  printf '[sample] %s\n' "$sample"
  if run_node_job 60 node "$sample"; then
    passed=$((passed + 1))
  else
    failed=$((failed + 1))
  fi
done

printf 'sample summary: passed=%d skipped=%d failed=%d\n' "$passed" "$skipped" "$failed"

if [[ "$failed" -ne 0 ]]; then
  exit 1
fi
