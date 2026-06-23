#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PUBSUB_RUNNER="$SCRIPT_DIR/../PubSub/run_e2e.sh"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$SCRIPT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"
echo "log_dir=$LOG_DIR"

ZLINK_CPP_E2E_BUILD_DIR="${ZLINK_CPP_E2E_BUILD_DIR:-}" \
  "$PUBSUB_RUNNER" >"$LOG_DIR/pubsub.stdout.log" 2>"$LOG_DIR/pubsub.stderr.log"

grep -q "scenario PS-C1 passed" "$LOG_DIR/pubsub.stdout.log"
NESTED_LOG_DIR="$(grep '^log_dir=' "$LOG_DIR/pubsub.stdout.log" | tail -n1 | cut -d= -f2-)"
if [[ -z "$NESTED_LOG_DIR" || ! -s "$NESTED_LOG_DIR/client-flow.log" ]]; then
  echo "client message flow log was not created" >&2
  exit 1
fi
if ! grep -q "corr=" "$NESTED_LOG_DIR/client-flow.log"; then
  echo "client message flow log did not include correlation markers" >&2
  exit 1
fi

echo "scenario MON-Baseline passed"
