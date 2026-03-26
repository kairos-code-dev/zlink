#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"
GUIDE_PATH="${SCRIPT_DIR}/core-system-posd-performance-first-ralph-guide.ko.md"
MASTER_PLAN_PATH="${GUIDE_PATH}"
LOGS_DIR="${SCRIPT_DIR}/logs"
MAX_ITERATIONS=100
POLL_SECONDS=30
STRESS_COUNT=10
GATE_LABEL="posd_perf_first_gate"
MODEL_ARG=(--model "gpt-5.4")

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Run the performance-first POSD Ralph loop from doc/plan/refactor/3nd.
The default authority document is:
  ${GUIDE_PATH}

Options:
  --guide PATH          Override execution guide path
                        (default: ${GUIDE_PATH})
  --master-plan PATH    Override the secondary authority path passed
                        to the supervisor. By default it reuses --guide.
                        (default: ${MASTER_PLAN_PATH})
  --logs-dir PATH       Override log directory
                        (default: ${LOGS_DIR})
  --max-iterations N    Maximum Codex supervisor iterations
                        (default: ${MAX_ITERATIONS})
  --poll-seconds N      Poll interval while a gate is running
                        (default: ${POLL_SECONDS})
  --stress-count N      Pass-through gate repeat count
                        (default: ${STRESS_COUNT})
  --gate-label NAME     Gate status label prefix
                        (default: ${GATE_LABEL})
  --model MODEL         Override the default codex model
                        (default: gpt-5.4)
  -h, --help            Show this help text

Examples:
  ./run_posd_perf_first_ralph_loop.sh
  ./run_posd_perf_first_ralph_loop.sh --max-iterations 30
  ./run_posd_perf_first_ralph_loop.sh --model gpt-5.4 --stress-count 20
EOF
}

cleanup() {
  local exit_rc=$?
  exit "${exit_rc}"
}

trap cleanup EXIT INT TERM

while [[ $# -gt 0 ]]; do
  case "$1" in
    --guide)
      GUIDE_PATH="$2"
      shift 2
      ;;
    --master-plan)
      MASTER_PLAN_PATH="$2"
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
    --poll-seconds)
      POLL_SECONDS="$2"
      shift 2
      ;;
    --stress-count)
      STRESS_COUNT="$2"
      shift 2
      ;;
    --gate-label)
      GATE_LABEL="$2"
      shift 2
      ;;
    --model)
      MODEL_ARG=(--model "$2")
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

if [[ ! -f "${GUIDE_PATH}" ]]; then
  echo "Execution guide not found: ${GUIDE_PATH}" >&2
  exit 1
fi

if [[ ! -f "${MASTER_PLAN_PATH}" ]]; then
  echo "Master plan not found: ${MASTER_PLAN_PATH}" >&2
  exit 1
fi

mkdir -p "${LOGS_DIR}"

mapfile -t existing_supervisors < <(
  pgrep -af "${ROOT_DIR}/core/tools/run_codex_execution_guide_loop.sh" | \
    awk -v self="$$" '$1 != self {print}'
)

if [[ "${#existing_supervisors[@]}" -gt 0 ]]; then
  echo "Existing execution supervisor detected. Stop it before starting a new Ralph loop." >&2
  printf '%s\n' "${existing_supervisors[@]}" >&2
  exit 16
fi

echo "=== POSD performance-first Ralph loop start ==="
echo "Root: ${ROOT_DIR}"
echo "Guide: ${GUIDE_PATH}"
echo "Master plan: ${MASTER_PLAN_PATH}"
echo "Logs dir: ${LOGS_DIR}"
echo "Gate label: ${GATE_LABEL}"
echo "Max iterations: ${MAX_ITERATIONS}"
echo "Stress count: ${STRESS_COUNT}"

cd "${ROOT_DIR}"

"${ROOT_DIR}/core/tools/run_codex_execution_guide_loop.sh" \
  --guide "${GUIDE_PATH}" \
  --master-plan "${MASTER_PLAN_PATH}" \
  --logs-dir "${LOGS_DIR}" \
  --max-iterations "${MAX_ITERATIONS}" \
  --poll-seconds "${POLL_SECONDS}" \
  --gate-label "${GATE_LABEL}" \
  --stress-count "${STRESS_COUNT}" \
  "${MODEL_ARG[@]}"
