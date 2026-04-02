#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"
GUIDE_PATH="${SCRIPT_DIR}/java-socket-surface-execution-guide.ko.md"
MASTER_PLAN_PATH="${GUIDE_PATH}"
SUPERVISOR_PATH="${ROOT_DIR}/core/tools/ralphloop/run_codex_execution_guide_loop.sh"
LOGS_DIR="${SCRIPT_DIR}/logs"
MAX_ITERATIONS=100
POLL_SECONDS=30
GATE_LABEL="java_socket_surface_split"
MODEL_ARG=()

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Run the Java socket surface split Ralphloop.

Execution guide authority:
  ${GUIDE_PATH}

Options:
  --guide PATH          Override execution guide path
                        (default: ${GUIDE_PATH})
  --master-plan PATH    Override legacy secondary path
                        New runs should normally leave this equal to --guide.
                        (default: ${MASTER_PLAN_PATH})
  --logs-dir PATH       Override log directory
                        (default: ${LOGS_DIR})
  --max-iterations N    Maximum Codex iterations
                        (default: ${MAX_ITERATIONS})
  --poll-seconds N      Poll interval while a gate is running
                        (default: ${POLL_SECONDS})
  --gate-label NAME     Gate label
                        (default: ${GATE_LABEL})
  --model MODEL         Pass --model MODEL to codex exec
  -h, --help            Show this help text

Examples:
  ./run_java_socket_surface_execution.sh
  ./run_java_socket_surface_execution.sh --max-iterations 0
  ./run_java_socket_surface_execution.sh --model gpt-5.4
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

if [[ ! -x "${SUPERVISOR_PATH}" ]]; then
  echo "Supervisor script not executable: ${SUPERVISOR_PATH}" >&2
  exit 1
fi

mkdir -p "${LOGS_DIR}"

echo "=== Java socket surface Ralphloop start ==="
echo "Root: ${ROOT_DIR}"
echo "Guide: ${GUIDE_PATH}"
echo "Master plan: ${MASTER_PLAN_PATH}"
echo "Logs dir: ${LOGS_DIR}"
echo "Gate label: ${GATE_LABEL}"
echo "Max iterations: ${MAX_ITERATIONS}"

cd "${ROOT_DIR}"

set +e
"${SUPERVISOR_PATH}" \
  --guide "${GUIDE_PATH}" \
  --master-plan "${MASTER_PLAN_PATH}" \
  --logs-dir "${LOGS_DIR}" \
  --max-iterations "${MAX_ITERATIONS}" \
  --poll-seconds "${POLL_SECONDS}" \
  --gate-label "${GATE_LABEL}" \
  "${MODEL_ARG[@]}"
rc=$?
set -e

if [[ "${MAX_ITERATIONS}" == "0" ]] && [[ "${rc}" -eq 0 || "${rc}" -eq 3 ]]; then
  echo "Smoke check passed: supervisor invocation path is valid."
  exit 0
fi

exit "${rc}"
