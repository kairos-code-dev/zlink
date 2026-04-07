#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
LOG_FILTER="${SCRIPT_DIR}/filter_manager_log.awk"
LOGS_DIR=""
MAX_ITERATIONS=0
MODEL_ARG=()
REASONING_EFFORT=""

usage() {
  cat <<EOF
Usage: $(basename "$0") [options] /abs/path/to/spec.md

Run the manager skill repeatedly against a spec document until the generated
manager guide reports completion or a blocked state.

Options:
  --max-iterations N    Maximum manager invocations before stopping.
                        Use 0 for unlimited. (default: ${MAX_ITERATIONS})
  --logs-dir PATH       Directory for per-iteration logs.
                        (default: sibling 'logs/<stem>.manager/' next to the spec;
                        manager guide: sibling 'logs/<stem>.manager.md')
  --model MODEL         Pass --model MODEL to codex exec.
  --reasoning-effort E  Pass -c model_reasoning_effort=E to codex exec.
  -h, --help            Show this help text.

Termination:
  - exit 0 when the manager guide says:
      status: complete
      remaining_tasks: 0
      completion_verified: true
  - exit 2 when the manager guide says:
      status: blocked
      blocked_reason: <non-empty>
    or the final message starts with:
      사용자 입력 필요:
  - exit 3 when max iterations is reached without completion.
  - exit non-zero on wrapper or codex execution errors.
EOF
}

trim_whitespace() {
  local value="$1"
  value="${value#"${value%%[![:space:]]*}"}"
  value="${value%"${value##*[![:space:]]}"}"
  printf '%s' "${value}"
}

is_nonnegative_integer() {
  [[ "$1" =~ ^[0-9]+$ ]]
}

require_file() {
  local path="$1"
  if [[ ! -f "${path}" ]]; then
    echo "Missing file: ${path}" >&2
    exit 1
  fi
}

read_guide_field() {
  local guide_path="$1"
  local field_name="$2"
  sed -n "s/^${field_name}: //p" "${guide_path}" | head -n 1
}

spec_stem_path() {
  local spec_path="$1"
  local no_md leaf dir suffix
  if [[ "${spec_path}" != *.md ]]; then
    printf '%s' "${spec_path}"
    return 0
  fi

  no_md="${spec_path%.md}"
  leaf="$(basename "${no_md}")"
  dir="$(dirname "${spec_path}")"

  if [[ "${leaf}" == *.* ]]; then
    suffix="${leaf##*.}"
    if [[ "${suffix}" =~ ^[A-Za-z]{2}([_-][A-Za-z]{2})?$ ]]; then
      leaf="${leaf%.*}"
    fi
  fi

  printf '%s/%s' "${dir}" "${leaf}"
}

guide_path_for_spec() {
  local spec_path="$1"
  printf '%s/logs/%s.manager.md' \
    "$(dirname "${spec_path}")" \
    "$(basename "$(spec_stem_path "${spec_path}")")"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --max-iterations)
      MAX_ITERATIONS="$2"
      shift 2
      ;;
    --logs-dir)
      LOGS_DIR="$2"
      shift 2
      ;;
    --model)
      MODEL_ARG=(--model "$2")
      shift 2
      ;;
    --reasoning-effort)
      REASONING_EFFORT="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      break
      ;;
    -*)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
    *)
      break
      ;;
  esac
done

if [[ $# -ne 1 ]]; then
  usage >&2
  exit 1
fi

if ! is_nonnegative_integer "${MAX_ITERATIONS}"; then
  echo "--max-iterations must be a non-negative integer" >&2
  exit 1
fi

SPEC_PATH="$1"
if [[ "${SPEC_PATH}" != /* ]]; then
  SPEC_PATH="$(cd "$(dirname "${SPEC_PATH}")" && pwd)/$(basename "${SPEC_PATH}")"
fi

require_file "${SPEC_PATH}"
require_file "${LOG_FILTER}"
GUIDE_PATH="$(guide_path_for_spec "${SPEC_PATH}")"

if [[ -z "${LOGS_DIR}" ]]; then
  LOGS_DIR="$(dirname "${SPEC_PATH}")/logs/$(basename "$(spec_stem_path "${SPEC_PATH}")").manager"
fi

mkdir -p "${LOGS_DIR}"

iteration=1

while true; do
  if [[ "${MAX_ITERATIONS}" -ne 0 ]] && [[ "${iteration}" -gt "${MAX_ITERATIONS}" ]]; then
    echo "Max iterations reached without completion: ${MAX_ITERATIONS}" >&2
    exit 3
  fi

  timestamp="$(date '+%Y%m%d_%H%M%S')"
  log_path="${LOGS_DIR}/manager_$(basename "$(spec_stem_path "${SPEC_PATH}")")_${timestamp}_iter${iteration}.log"

  echo "=== manager iteration ${iteration} ==="
  echo "spec: ${SPEC_PATH}"
  echo "guide: ${GUIDE_PATH}"
  echo "log: ${log_path}"

  codex_args=(
    exec
    --dangerously-bypass-approvals-and-sandbox
    -C "${ROOT_DIR}"
  )

  if [[ ${#MODEL_ARG[@]} -gt 0 ]]; then
    codex_args+=("${MODEL_ARG[@]}")
  fi
  if [[ -n "${REASONING_EFFORT}" ]]; then
    codex_args+=(-c "model_reasoning_effort=${REASONING_EFFORT}")
  fi

  set +e
  codex "${codex_args[@]}" "\$manager ${SPEC_PATH}" 2>&1 | awk -f "${LOG_FILTER}" | tee "${log_path}"
  codex_rc=${PIPESTATUS[0]}
  set -e

  if [[ "${codex_rc}" -ne 0 ]]; then
    echo "codex exec failed with exit code ${codex_rc}" >&2
    exit "${codex_rc}"
  fi

  if [[ ! -f "${GUIDE_PATH}" ]]; then
    echo "Manager guide was not created: ${GUIDE_PATH}" >&2
    exit 1
  fi

  status="$(trim_whitespace "$(read_guide_field "${GUIDE_PATH}" "status")")"
  remaining_tasks="$(trim_whitespace "$(read_guide_field "${GUIDE_PATH}" "remaining_tasks")")"
  completion_verified="$(trim_whitespace "$(read_guide_field "${GUIDE_PATH}" "completion_verified")")"
  blocked_reason="$(trim_whitespace "$(read_guide_field "${GUIDE_PATH}" "blocked_reason")")"

  echo "guide status: ${status:-<missing>}"
  echo "guide remaining_tasks: ${remaining_tasks:-<missing>}"
  echo "guide completion_verified: ${completion_verified:-<missing>}"
  echo "guide blocked_reason: ${blocked_reason:-<empty>}"

  if [[ "${status}" == "complete" ]] \
    && [[ "${remaining_tasks}" == "0" ]] \
    && [[ "${completion_verified}" == "true" ]]; then
    echo "Manager workflow completed."
    exit 0
  fi

  if [[ "${status}" == "blocked" ]]; then
    if [[ -n "${blocked_reason}" ]]; then
      echo "Manager workflow blocked: ${blocked_reason}" >&2
    else
      echo "Manager workflow blocked." >&2
    fi
    exit 2
  fi

  if grep -q '^사용자 입력 필요:' "${log_path}"; then
    echo "Manager requested user input." >&2
    exit 2
  fi

  iteration=$((iteration + 1))
done
