#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"
GUIDE_PATH="${SCRIPT_DIR}/core-system-posd-refactor-remaining-execution-guide.ko.md"
MASTER_PLAN_PATH="${SCRIPT_DIR}/core-system-posd-refactor-master-plan.ko.md"
LOGS_DIR="${SCRIPT_DIR}/logs"
MAX_ITERATIONS=100
POLL_SECONDS=30
STRESS_COUNT=1
GATE_LABEL="posd_refactor_gate"
MODEL_ARG=()
LOCK_ACQUIRED=0
CAMPAIGN_LOCK_DIR=""
CAMPAIGN_LOCK_OWNER=""

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Run the POSD refactor execution campaign from the 2nd refactor doc directory.
The todo authority is:
  ${GUIDE_PATH}

Options:
  --guide PATH          Override execution guide path
                        (default: ${GUIDE_PATH})
  --master-plan PATH    Override master plan path
                        (default: ${MASTER_PLAN_PATH})
  --logs-dir PATH       Override log directory
                        (default: ${LOGS_DIR})
  --max-iterations N    Maximum Codex supervisor iterations
                        (default: ${MAX_ITERATIONS})
  --poll-seconds N      Poll interval while a gate is running
                        (default: ${POLL_SECONDS})
  --stress-count N      Default thread-safe stress repeat count
                        (default: ${STRESS_COUNT})
  --gate-label NAME     Gate status label prefix
                        (default: ${GATE_LABEL})
  --model MODEL         Pass --model MODEL to codex exec
  -h, --help            Show this help text

Examples:
  ./run_posd_refactor_execution.sh
  ./run_posd_refactor_execution.sh --model gpt-5.4 --stress-count 20
EOF
}

cleanup() {
  local exit_rc=$?
  if [[ "${LOCK_ACQUIRED}" -eq 1 ]] && [[ -n "${CAMPAIGN_LOCK_DIR}" ]] \
    && [[ -d "${CAMPAIGN_LOCK_DIR}" ]]; then
    rm -rf "${CAMPAIGN_LOCK_DIR}"
  fi
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

CAMPAIGN_LOCK_DIR="${LOGS_DIR}/posd_refactor_execution.lock"
CAMPAIGN_LOCK_OWNER="${CAMPAIGN_LOCK_DIR}/owner"

if mkdir "${CAMPAIGN_LOCK_DIR}" 2>/dev/null; then
  LOCK_ACQUIRED=1
  {
    printf 'owner_pid=%s\n' "$$"
    printf 'started_at=%s\n' "$(date '+%Y-%m-%d %H:%M:%S %z')"
    printf 'guide=%s\n' "${GUIDE_PATH}"
  } > "${CAMPAIGN_LOCK_OWNER}"
else
  existing_pid=""
  if [[ -f "${CAMPAIGN_LOCK_OWNER}" ]]; then
    existing_pid="$(sed -n 's/^owner_pid=//p' "${CAMPAIGN_LOCK_OWNER}" | head -n 1)"
  fi
  if [[ -n "${existing_pid}" ]] && kill -0 "${existing_pid}" 2>/dev/null; then
    echo "Another POSD refactor execution campaign is already running (pid=${existing_pid})." >&2
    exit 16
  fi
  rm -rf "${CAMPAIGN_LOCK_DIR}"
  mkdir "${CAMPAIGN_LOCK_DIR}"
  LOCK_ACQUIRED=1
  {
    printf 'owner_pid=%s\n' "$$"
    printf 'started_at=%s\n' "$(date '+%Y-%m-%d %H:%M:%S %z')"
    printf 'guide=%s\n' "${GUIDE_PATH}"
  } > "${CAMPAIGN_LOCK_OWNER}"
fi

mapfile -t existing_supervisors < <(
  pgrep -af "${ROOT_DIR}/core/tools/run_codex_execution_guide_loop.sh" | \
    awk -v self="$$" '$1 != self {print}'
)

if [[ "${#existing_supervisors[@]}" -gt 0 ]]; then
  echo "Existing execution supervisor detected. Stop it before starting a new campaign." >&2
  printf '%s\n' "${existing_supervisors[@]}" >&2
  exit 16
fi

echo "=== POSD refactor execution start ==="
echo "Root: ${ROOT_DIR}"
echo "Guide: ${GUIDE_PATH}"
echo "Master plan: ${MASTER_PLAN_PATH}"
echo "Logs dir: ${LOGS_DIR}"
echo "Gate label: ${GATE_LABEL}"
echo "Stress count: ${STRESS_COUNT}"
echo "Max iterations: ${MAX_ITERATIONS}"

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
