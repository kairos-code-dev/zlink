#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"
GUIDE_PATH="${SCRIPT_DIR}/python-socket-surface-execution-guide.ko.md"
SUPERVISOR_PATH="${ROOT_DIR}/core/tools/ralphloop/run_codex_execution_guide_loop.sh"
LOGS_DIR="${SCRIPT_DIR}/logs"
MAX_ITERATIONS=100
POLL_SECONDS=30
STRESS_COUNT=1
GATE_LABEL="python_socket_surface_gate"
MODEL_ARG=()

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Run the Python socket-surface Ralph loop.
The execution guide authority is:
  ${GUIDE_PATH}

Options:
  --guide PATH          Override execution guide path
                        (default: ${GUIDE_PATH})
  --logs-dir PATH       Override log directory
                        (default: ${LOGS_DIR})
  --max-iterations N    Maximum Codex supervisor iterations
                        (default: ${MAX_ITERATIONS})
  --poll-seconds N      Poll interval while a gate is running
                        (default: ${POLL_SECONDS})
  --stress-count N      Default repeat count passed through to the gate loop
                        (default: ${STRESS_COUNT})
  --gate-label NAME     Gate status label prefix
                        (default: ${GATE_LABEL})
  --model MODEL         Pass --model MODEL to codex exec
  -h, --help            Show this help text

Examples:
  ./run_python_socket_surface_loop.sh
  ./run_python_socket_surface_loop.sh --max-iterations 0
  ./run_python_socket_surface_loop.sh --model gpt-5.4
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --guide)
      GUIDE_PATH="$2"
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

GUIDE_PATH="$(realpath -m "${GUIDE_PATH}")"
LOGS_DIR="$(realpath -m "${LOGS_DIR}")"

if [[ ! -f "${GUIDE_PATH}" ]]; then
  echo "Execution guide not found: ${GUIDE_PATH}" >&2
  exit 1
fi

if [[ ! -x "${SUPERVISOR_PATH}" ]]; then
  echo "Supervisor script not executable: ${SUPERVISOR_PATH}" >&2
  exit 1
fi

mkdir -p "${LOGS_DIR}"

echo "=== Python socket surface loop start ==="
echo "Root: ${ROOT_DIR}"
echo "Guide: ${GUIDE_PATH}"
echo "Logs dir: ${LOGS_DIR}"
echo "Gate label: ${GATE_LABEL}"
echo "Stress count: ${STRESS_COUNT}"
echo "Max iterations: ${MAX_ITERATIONS}"
echo "Execution lock: disabled"

cd "${ROOT_DIR}"

set +e
"${SUPERVISOR_PATH}" \
  --guide "${GUIDE_PATH}" \
  --master-plan "${GUIDE_PATH}" \
  --logs-dir "${LOGS_DIR}" \
  --max-iterations "${MAX_ITERATIONS}" \
  --poll-seconds "${POLL_SECONDS}" \
  --gate-label "${GATE_LABEL}" \
  --stress-count "${STRESS_COUNT}" \
  "${MODEL_ARG[@]}"
rc=$?
set -e

if [[ "${MAX_ITERATIONS}" == "0" ]] && [[ "${rc}" -eq 3 ]]; then
  echo "Smoke check passed: supervisor invocation path is valid."
  exit 0
fi

exit "${rc}"
