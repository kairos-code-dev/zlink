#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
GUIDE_PATH="${SCRIPT_DIR}/single-libzmq-gap-ralph-guide.ko.md"
LOGS_DIR="${SCRIPT_DIR}/logs"
GATE_LABEL="single_libzmq_gap_perf"
STRESS_COUNT=1

export RALPH_LOOP_DISPLAY_NAME="$(basename "$0")"

exec "${ROOT_DIR}/core/tools/ralphloop/run_codex_execution_guide_loop.sh" \
  --guide "${GUIDE_PATH}" \
  --master-plan "${GUIDE_PATH}" \
  --logs-dir "${LOGS_DIR}" \
  --gate-label "${GATE_LABEL}" \
  --stress-count "${STRESS_COUNT}" \
  --model "gpt-5.4" \
  --reasoning-effort "medium" \
  "$@"
