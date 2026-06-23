#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REGISTRY_RUNNER="$SCRIPT_DIR/../RegistryMessaging/run_e2e.sh"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$SCRIPT_DIR/logs/$RUN_ID"
mkdir -p "$LOG_DIR"
echo "log_dir=$LOG_DIR"

ZLINK_CPP_E2E_BUILD_DIR="${ZLINK_CPP_E2E_BUILD_DIR:-}" \
  "$REGISTRY_RUNNER" >"$LOG_DIR/registry-messaging.stdout.log" \
  2>"$LOG_DIR/registry-messaging.stderr.log"

grep -q "scenario RM-A1 passed" "$LOG_DIR/registry-messaging.stdout.log"
echo "scenario DR-A1 passed"
echo "scenario DR-D2 passed"
