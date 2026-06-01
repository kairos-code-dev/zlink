#!/usr/bin/env bash
set -euo pipefail

# JavaScript actor samples share the Node binding runtime (@zlink-systems/zlink).
# No separate native binding exists; these plain-JS samples consume the built
# Node package directly. Build the Node binding first: (cd ../../node && npm run build).
SAMPLES_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NODE_DIR="$(cd "$SAMPLES_DIR/../../node" && pwd)"

if [[ ! -f "$NODE_DIR/dist/index.js" || ! -f "$NODE_DIR/build/Release/zlink.node" ]]; then
  echo "node binding is not built; run: (cd $NODE_DIR && npm run build)" >&2
  exit 1
fi

# Link the shared runtime so bare `require('@zlink-systems/zlink')` resolves to
# the sibling Node binding without publishing or copying it.
mkdir -p "$SAMPLES_DIR/node_modules/@zlink-systems"
ln -sfn "$NODE_DIR" "$SAMPLES_DIR/node_modules/@zlink-systems/zlink"

samples=(
  "actor_room_server_sample.js"
  "actor_gateway_relay_sample.js"
  "actor_single_player_queue_sample.js"
)

passed=0
failed=0
for sample in "${samples[@]}"; do
  printf '[sample] %s\n' "$sample"
  if node "$SAMPLES_DIR/$sample"; then
    passed=$((passed + 1))
  else
    failed=$((failed + 1))
  fi
done

printf 'sample summary: passed=%d failed=%d\n' "$passed" "$failed"
[[ "$failed" -eq 0 ]]
