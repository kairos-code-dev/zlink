#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"
GUIDE_PATH="${SCRIPT_DIR}/gateway-removal-metadata-execution-guide.ko.md"
MASTER_PLAN_PATH="${GUIDE_PATH}"
LOGS_DIR="${SCRIPT_DIR}/logs"
MAX_ITERATIONS=100
POLL_SECONDS=30
STRESS_COUNT=1
GATE_LABEL="gateway_removal_metadata_gate"
MODEL_ARG=()
MASTER_PLAN_EXPLICIT=0
SUPERVISOR_PID=""
GATE_STATUS_FILE=""

pid_state() {
  local pid="$1"
  ps -o stat= -p "${pid}" 2>/dev/null | awk 'NR==1 {print $1}'
}

pid_is_stopped() {
  local pid="$1"
  local state=""
  state="$(pid_state "${pid}")"
  [[ -n "${state}" ]] && [[ "${state}" == *T* ]]
}

terminate_pid_group() {
  local pid="$1"
  local pgid=""
  local waited=0

  if [[ -z "${pid}" ]] || ! kill -0 "${pid}" 2>/dev/null; then
    return 0
  fi

  pgid="$(ps -o pgid= -p "${pid}" 2>/dev/null | tr -d '[:space:]')"

  if pid_is_stopped "${pid}"; then
    if [[ -n "${pgid}" ]]; then
      kill -CONT -- "-${pgid}" 2>/dev/null || true
    else
      kill -CONT "${pid}" 2>/dev/null || true
    fi
    sleep 1
  fi

  if [[ -n "${pgid}" ]]; then
    kill -TERM -- "-${pgid}" 2>/dev/null || true
  fi
  kill -TERM "${pid}" 2>/dev/null || true

  while kill -0 "${pid}" 2>/dev/null && [[ "${waited}" -lt 5 ]]; do
    sleep 1
    waited=$((waited + 1))
  done

  if kill -0 "${pid}" 2>/dev/null; then
    if [[ -n "${pgid}" ]]; then
      kill -KILL -- "-${pgid}" 2>/dev/null || true
    fi
    kill -KILL "${pid}" 2>/dev/null || true
  fi

  wait "${pid}" 2>/dev/null || true
}

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Run the gateway removal / metadata follow-up execution campaign.
The execution guide authority is:
  ${GUIDE_PATH}

The master plan authority is the same execution guide file.

Options:
  --guide PATH          Override execution guide path
                        (default: ${GUIDE_PATH})
  --master-plan PATH    Override secondary authority path
                        (default: same as --guide)
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
  ./run_gateway_removal_metadata_execution.sh
  ./run_gateway_removal_metadata_execution.sh --model gpt-5.4 --stress-count 10
EOF
}

cleanup() {
  local exit_rc=$?
  local gate_owner_pid=""

  terminate_pid_group "${SUPERVISOR_PID}"

  if [[ -n "${GATE_STATUS_FILE}" ]] && [[ -f "${GATE_STATUS_FILE}" ]]; then
    gate_owner_pid="$(sed -n 's/^owner_pid=//p' "${GATE_STATUS_FILE}" | head -n 1)"
    terminate_pid_group "${gate_owner_pid}"
  fi
  exit "${exit_rc}"
}

trap cleanup EXIT INT TERM TSTP

while [[ $# -gt 0 ]]; do
  case "$1" in
    --guide)
      GUIDE_PATH="$2"
      if [[ "${MASTER_PLAN_EXPLICIT}" -eq 0 ]]; then
        MASTER_PLAN_PATH="$2"
      fi
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
GATE_STATUS_FILE="${LOGS_DIR}/${GATE_LABEL}.status"

echo "=== Gateway removal / metadata execution start ==="
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
  "${MODEL_ARG[@]}" &
SUPERVISOR_PID=$!
wait "${SUPERVISOR_PID}"
