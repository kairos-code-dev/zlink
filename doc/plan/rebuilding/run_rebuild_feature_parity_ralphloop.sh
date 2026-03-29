#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
GUIDE_PATH="${SCRIPT_DIR}/rebuild-feature-parity-ralph-guide.ko.md"
MAIN_PLAN_PATH="${SCRIPT_DIR}/rebuild-feature-parity-plan.ko.md"
LOGS_DIR="${SCRIPT_DIR}/logs/rebuild-feature-parity-ralphloop"

export RALPH_LOOP_DISPLAY_NAME="$(basename "$0")"

exec "${ROOT_DIR}/core/tools/ralphloop/run_codex_execution_guide_loop.sh" \
  --guide "${GUIDE_PATH}" \
  --master-plan "${MAIN_PLAN_PATH}" \
  --logs-dir "${LOGS_DIR}" \
  --max-iterations 0 \
  --poll-seconds 30 \
  --gate-label rebuild_feature_parity \
  --stress-count 10 \
  "$@"
