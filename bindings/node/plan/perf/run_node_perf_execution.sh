#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"
GUIDE_PATH="${SCRIPT_DIR}/node-perf-execution-guide.ko.md"
MASTER_PLAN_PATH="${GUIDE_PATH}"
MASTER_PLAN_EXPLICIT=0
SUPERVISOR_PATH="${ROOT_DIR}/core/tools/ralphloop/run_codex_execution_guide_loop.sh"
LOGS_DIR="${SCRIPT_DIR}/logs"
MAX_ITERATIONS=0
INIT_ONLY=0
POLL_SECONDS=30
GATE_LABEL="node_perf_execution"
STRESS_COUNT=1
MODEL_ARG=()
REASONING_EFFORT_ARG=()

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Run the Node perf execution campaign until the execution guide is fully applied.
The single execution authority is:
  ${GUIDE_PATH}

Options:
  --guide PATH          Override execution guide path
                        (default: ${GUIDE_PATH})
  --master-plan PATH    Legacy compatibility path
                        (default: guide path)
  --logs-dir PATH       Override log directory
                        (default: ${LOGS_DIR})
  --max-iterations N    Maximum Codex supervisor iterations
                        Use 0 for unlimited.
                        (default: ${MAX_ITERATIONS})
  --init-only           Initialize logs/session path and exit without
                        starting Codex
  --poll-seconds N      Poll interval while a gate is running
                        (default: ${POLL_SECONDS})
  --gate-label NAME     Gate status label prefix
                        (default: ${GATE_LABEL})
  --stress-count N      Forwarded default gate repeat count
                        (default: ${STRESS_COUNT})
  --model MODEL         Pass --model MODEL to codex exec
  --reasoning-effort E  Pass --reasoning-effort E to the supervisor
  -h, --help            Show this help text

Examples:
  ./bindings/node/plan/perf/run_node_perf_execution.sh
  ./bindings/node/plan/perf/run_node_perf_execution.sh --init-only
  ./bindings/node/plan/perf/run_node_perf_execution.sh --model gpt-5.4
  ./bindings/node/plan/perf/run_node_perf_execution.sh --reasoning-effort high
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --guide)
      GUIDE_PATH="$2"
      shift 2
      ;;
    --master-plan)
      MASTER_PLAN_PATH="$2"
      MASTER_PLAN_EXPLICIT=1
      shift 2
      ;;
    --logs-dir)
      LOGS_DIR="$2"
      shift 2
      ;;
    --max-iterations)
      MAX_ITERATIONS="$2"
      shift 2
      ;;
    --init-only)
      INIT_ONLY=1
      shift
      ;;
    --poll-seconds)
      POLL_SECONDS="$2"
      shift 2
      ;;
    --gate-label)
      GATE_LABEL="$2"
      shift 2
      ;;
    --stress-count)
      STRESS_COUNT="$2"
      shift 2
      ;;
    --model)
      MODEL_ARG=(--model "$2")
      shift 2
      ;;
    --reasoning-effort)
      REASONING_EFFORT_ARG=(--reasoning-effort "$2")
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ "${MASTER_PLAN_EXPLICIT}" -eq 0 ]]; then
  MASTER_PLAN_PATH="${GUIDE_PATH}"
fi

if [[ ! -f "${GUIDE_PATH}" ]]; then
  echo "Execution guide not found: ${GUIDE_PATH}" >&2
  exit 1
fi

if [[ ! -f "${MASTER_PLAN_PATH}" ]]; then
  echo "Master plan not found: ${MASTER_PLAN_PATH}" >&2
  exit 1
fi

if [[ ! -x "${SUPERVISOR_PATH}" ]]; then
  echo "Supervisor script not executable: ${SUPERVISOR_PATH}" >&2
  exit 1
fi

mkdir -p "${LOGS_DIR}"

echo "=== Node perf execution start ==="
echo "Root: ${ROOT_DIR}"
echo "Guide: ${GUIDE_PATH}"
echo "Legacy master plan: ${MASTER_PLAN_PATH}"
echo "Logs dir: ${LOGS_DIR}"
echo "Gate label: ${GATE_LABEL}"
echo "Stress count: ${STRESS_COUNT}"
echo "Max iterations: ${MAX_ITERATIONS}"
echo "Execution lock: disabled"
if [[ "${#MODEL_ARG[@]}" -gt 0 ]]; then
  echo "Model override: ${MODEL_ARG[1]}"
fi
if [[ "${#REASONING_EFFORT_ARG[@]}" -gt 0 ]]; then
  echo "Reasoning effort: ${REASONING_EFFORT_ARG[1]}"
fi

cd "${ROOT_DIR}"

set +e
"${SUPERVISOR_PATH}" \
  --guide "${GUIDE_PATH}" \
  --master-plan "${MASTER_PLAN_PATH}" \
  --logs-dir "${LOGS_DIR}" \
  --max-iterations "${MAX_ITERATIONS}" \
  $([[ "${INIT_ONLY}" -eq 1 ]] && printf '%s' "--init-only") \
  --poll-seconds "${POLL_SECONDS}" \
  --gate-label "${GATE_LABEL}" \
  --stress-count "${STRESS_COUNT}" \
  "${MODEL_ARG[@]}" \
  "${REASONING_EFFORT_ARG[@]}"
rc=$?
set -e

exit "${rc}"
