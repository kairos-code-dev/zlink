#!/usr/bin/env bash
set -euo pipefail

SAMPLES_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SAMPLES_DIR/.." && pwd)"
cd "$ROOT_DIR"

source "$ROOT_DIR/scripts/run_node_job.sh"

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

passed=0
failed=0

for sample in "${samples[@]}"; do
  printf '[sample] %s\n' "$sample"
  if run_node_job 60 node "$sample"; then
    passed=$((passed + 1))
  else
    failed=$((failed + 1))
  fi
done

printf 'sample summary: passed=%d failed=%d\n' "$passed" "$failed"

if [[ "$failed" -ne 0 ]]; then
  exit 1
fi
