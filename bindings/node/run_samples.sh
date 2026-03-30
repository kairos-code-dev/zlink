#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

samples=(
  "dist-tools/examples/pair_recv_sample.js"
  "dist-tools/examples/pair_callback_sample.js"
  "dist-tools/examples/pubsub_recv_sample.js"
  "dist-tools/examples/pubsub_callback_sample.js"
  "dist-tools/examples/dealer_router_recv_sample.js"
  "dist-tools/examples/spot_recv_sample.js"
  "dist-tools/examples/spot_callback_sample.js"
  "dist-tools/examples/stream_recv_sample.js"
  "dist-tools/examples/stream_callback_sample.js"
  "dist-tools/examples/monitor_sample.js"
  "dist-tools/examples/discovery_registry_sample.js"
)

passed=0
failed=0

for sample in "${samples[@]}"; do
  printf '[sample] %s\n' "$sample"
  if node "$sample"; then
    passed=$((passed + 1))
  else
    failed=$((failed + 1))
  fi
done

printf 'sample summary: passed=%d failed=%d\n' "$passed" "$failed"

if [[ "$failed" -ne 0 ]]; then
  exit 1
fi
