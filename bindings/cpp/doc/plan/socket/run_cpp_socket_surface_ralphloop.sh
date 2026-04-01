#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"
GUIDE_PATH="${SCRIPT_DIR}/cpp-socket-surface-implementation-ralph-guide.ko.md"
RALPHLOOP_PATH="${ROOT_DIR}/core/tools/ralphloop/run_codex_execution_guide_loop.sh"
LOGS_DIR="${SCRIPT_DIR}/logs"
MAX_ITERATIONS=100
POLL_SECONDS=30
GATE_LABEL="cpp_socket_surface_impl_gate"
STRESS_COUNT=1
MODEL_ARG=()

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Run the C++ socket-surface implementation Ralph loop.

Authority guide:
  ${GUIDE_PATH}

Options:
  --guide PATH          Override guide path
                        (default: ${GUIDE_PATH})
  --logs-dir PATH       Override logs directory
                        (default: ${LOGS_DIR})
  --max-iterations N    Maximum Ralph loop iterations
                        (default: ${MAX_ITERATIONS})
  --poll-seconds N      Gate poll interval
                        (default: ${POLL_SECONDS})
  --gate-label NAME     Gate label prefix
                        (default: ${GATE_LABEL})
  --stress-count N      Pass-through stress count for supervisor
                        (default: ${STRESS_COUNT})
  --model MODEL         Pass --model MODEL to codex exec
  -h, --help            Show this help text

Examples:
  ./run_cpp_socket_surface_ralphloop.sh
  ./run_cpp_socket_surface_ralphloop.sh --max-iterations 0
  ./run_cpp_socket_surface_ralphloop.sh --model gpt-5.4
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
  echo "Guide not found: ${GUIDE_PATH}" >&2
  exit 1
fi

if [[ ! -x "${RALPHLOOP_PATH}" ]]; then
  echo "Ralphloop supervisor not executable: ${RALPHLOOP_PATH}" >&2
  exit 1
fi

mkdir -p "${LOGS_DIR}"

echo "=== C++ socket surface Ralph loop start ==="
echo "Root: ${ROOT_DIR}"
echo "Guide: ${GUIDE_PATH}"
echo "Logs dir: ${LOGS_DIR}"
echo "Gate label: ${GATE_LABEL}"
echo "Max iterations: ${MAX_ITERATIONS}"
echo "Lock policy: disabled"

cd "${ROOT_DIR}"

set +e
"${RALPHLOOP_PATH}" \
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
  echo "Smoke check passed: Ralph loop invocation path is valid."
  exit 0
fi

exit "${rc}"
