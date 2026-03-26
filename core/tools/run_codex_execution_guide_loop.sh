#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
GUIDE_PATH="${ROOT_DIR}/doc/plan/refactor/2nd/core-system-posd-refactor-remaining-execution-guide.ko.md"
MASTER_PLAN_PATH="${ROOT_DIR}/doc/plan/refactor/2nd/core-system-posd-refactor-master-plan.ko.md"
LOGS_DIR="${ROOT_DIR}/doc/plan/refactor/2nd/logs"
MAX_ITERATIONS=100
POLL_SECONDS=30
GATE_LABEL="phase2_thread_safe_stress"
STRESS_COUNT=1
MODEL_ARG=()
CODEX_ARGS=(
  exec
  --dangerously-bypass-approvals-and-sandbox
  -C "${ROOT_DIR}"
)

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Run Codex repeatedly against an execution guide and master plan until the guide
is fully applied or Codex reports that user input is required.

Options:
  --guide PATH          Execution guide path
                        (default: ${GUIDE_PATH})
  --master-plan PATH    Master plan path
                        (default: ${MASTER_PLAN_PATH})
  --logs-dir PATH       Log directory
                        (default: ${LOGS_DIR})
  --max-iterations N    Maximum Codex iterations
                        (default: ${MAX_ITERATIONS})
  --poll-seconds N      Waiting interval while a long-running gate is active
                        (default: ${POLL_SECONDS})
  --gate-label NAME     Gate status label to watch
                        (default: ${GATE_LABEL})
  --stress-count N      Default repeat count to pass to run_execution_gate_loop.sh
                        (default: ${STRESS_COUNT})
  --model MODEL         Pass --model MODEL to codex exec
  -h, --help            Show this help text

Termination contract:
  - If the Codex final message is exactly '미적용 사항이 없습니다.' the loop exits 0.
  - If the Codex final message starts with '사용자 입력 필요:' the loop exits 2.
  - If the Codex final message is exactly '계속 진행 필요' the loop continues.
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
  echo "Execution guide not found: ${GUIDE_PATH}" >&2
  exit 1
fi
if [[ ! -f "${MASTER_PLAN_PATH}" ]]; then
  echo "Master plan not found: ${MASTER_PLAN_PATH}" >&2
  exit 1
fi

mkdir -p "${LOGS_DIR}"

timestamp="$(date '+%Y%m%d_%H%M%S')"
session_dir="${LOGS_DIR}/codex_execution_guide_loop_${timestamp}"
gate_status_file="${LOGS_DIR}/${GATE_LABEL}.status"
mkdir -p "${session_dir}"

cat <<EOF
=== Codex execution guide loop start ===
Guide: ${GUIDE_PATH}
Master plan: ${MASTER_PLAN_PATH}
Session dir: ${session_dir}
Max iterations: ${MAX_ITERATIONS}
Gate status file: ${gate_status_file}
Stress count: ${STRESS_COUNT}
EOF

load_gate_field() {
  local file_path="$1"
  local key="$2"
  sed -n "s/^${key}=//p" "${file_path}" | head -n 1
}

wait_for_running_gate() {
  local gate_status
  local gate_stage
  local gate_owner_pid
  local gate_log

  gate_status="$(load_gate_field "${gate_status_file}" status)"
  gate_stage="$(load_gate_field "${gate_status_file}" stage)"
  gate_owner_pid="$(load_gate_field "${gate_status_file}" owner_pid)"
  gate_log="$(load_gate_field "${gate_status_file}" stress_log)"

  if [[ "${gate_status}" != "running" ]]; then
    return 1
  fi

  if [[ -z "${gate_owner_pid}" ]] || ! kill -0 "${gate_owner_pid}" 2>/dev/null; then
    echo "=== Detected stale running gate status; releasing it ==="
    {
      printf 'status=stale\n'
      printf 'label=%s\n' "${GATE_LABEL}"
      printf 'updated_at=%s\n' "$(date '+%Y-%m-%d %H:%M:%S %z')"
      if [[ -n "${gate_log}" ]]; then
        printf 'stress_log=%s\n' "${gate_log}"
      fi
    } > "${gate_status_file}"
    rm -rf "${LOGS_DIR}/${GATE_LABEL}.lock"
    return 1
  fi

  echo "=== Long-running gate active; skipping new Codex iteration ==="
  echo "Owner pid: ${gate_owner_pid}"
  if [[ -n "${gate_stage}" ]]; then
    echo "Gate stage: ${gate_stage}"
  fi
  if [[ -n "${gate_log}" ]]; then
    echo "Gate log: ${gate_log}"
  fi

  while kill -0 "${gate_owner_pid}" 2>/dev/null; do
    sleep "${POLL_SECONDS}"
    if ! kill -0 "${gate_owner_pid}" 2>/dev/null; then
      break
    fi
    echo "=== Waiting for gate owner pid ${gate_owner_pid} ($(date '+%Y-%m-%d %H:%M:%S %z')) ==="
    if [[ -n "${gate_log}" ]] && [[ -f "${gate_log}" ]]; then
      tail -n 20 "${gate_log}" || true
    fi
  done

  echo "=== Long-running gate finished; Codex iterations may resume ==="
  return 0
}

iteration=1
while [[ "${iteration}" -le "${MAX_ITERATIONS}" ]]; do
  if [[ -f "${gate_status_file}" ]]; then
    if wait_for_running_gate; then
      continue
    fi
  fi

  iter_prefix="$(printf '%02d' "${iteration}")"
  prompt_file="${session_dir}/${iter_prefix}_prompt.txt"
  run_log="${session_dir}/${iter_prefix}_codex.log"
  last_message="${session_dir}/${iter_prefix}_last_message.txt"

  cat > "${prompt_file}" <<EOF
/home/hep7/project/kairos/zlink/AGENTS.md 지침과 저장소 규칙을 따른다.

작업 목표:
- ${GUIDE_PATH}
  이 문서의 남은 작업을 중단 없이 끝까지 진행한다.
- ${MASTER_PLAN_PATH}
  이 문서의 실제 구현 내용이 코드에 반영되도록 작업한다.

작업 규칙:
- 실행 가이드와 마스터 플랜을 둘 다 authority로 사용한다.
- 실행 순서와 완료 판정은 실행 가이드를 따르되, 실제 구현 내용과 설계 intent는 마스터 플랜을 기준으로 확인한다.
- 각 작업 묶음을 끝낼 때마다 마스터 플랜 전체와 실행 가이드 전체를 다시 훑고 아직 코드에 반영되지 않은 구현 항목이 남아 있는지 확인한다.
- 실행 가이드 체크리스트가 green이어도 마스터 플랜의 실제 구현 내용이 아직 덜 반영됐으면 완료로 처리하지 않는다.
- 마스터 플랜에 있는데 실행 가이드 체크리스트에 없는 구현 항목을 발견하면 실행 가이드를 먼저 갱신한 뒤 작업을 계속한다.
- 마스터 플랜과 실행 가이드가 어긋나면 실행 가이드를 먼저 고치고 그 다음 코드를 진행한다.
- 문서의 첫 미완료 항목부터 순서대로 진행한다.
- core 버그 수정 요청 범위는 core/ 와 core/tests/ 로 제한한다.
- core/build/ 만 사용한다.
- 장시간 gate가 필요하면 ./core/tools/run_execution_gate_loop.sh --logs-dir ${LOGS_DIR} --label ${GATE_LABEL} --count ${STRESS_COUNT} 를 최소 기준으로 사용해 같은 셸 프로세스에서 끝까지 추적한다.
- flake 재현, 신뢰도 보강, 추가 확인이 필요하다고 판단하면 thread-safe stress count를 ${STRESS_COUNT}보다 더 크게 올릴 수 있다.
- 장시간 gate 실패 시 문서 규칙대로 단일 재현, core 수정, 재빌드, 원래 gate 재실행까지 처리한다.
- 문서 상태표와 체크리스트도 실제 진행 상태에 맞게 갱신한다.
- 가이드나 마스터 플랜에 단계별 commit / push 규칙이 있으면 그대로 따른다.
- unrelated 변경은 commit/push에 섞지 않는다. 현재 단계 범위만 안전하게 commit할 수 없으면 완료로 닫지 않는다.
- push한 commit hash를 문서의 검증 증거 또는 진행 메모에 남길 수 있으면 남긴다.
- routine한 판단은 사용자에게 묻지 말고 스스로 진행한다.
- 정말 필요한 사용자 결정이 아니면 멈추지 않는다.
- stress/lane/perf/functional regression은 구현 완료를 증명하는 보조 수단이지 구현 내용 자체를 대체하지 않는다.
- 테스트 통과만으로 마스터 플랜 구현 내용이 반영됐다고 추정하지 않는다.

종료 판정 규칙:
- 실행 가이드 기준으로 더 이상 미적용 사항이 없고 다음에 할 작업이 전혀 없을 때만 정확히 아래 한 줄만 출력한다.
미적용 사항이 없습니다.

- 사용자 결정 없이는 더 진행할 수 없는 blocker가 있을 때만 정확히 아래 형식 한 줄만 출력한다.
사용자 입력 필요: <한 줄 이유>

- 그 외에는 이번 iteration 안에서 할 수 있는 작업을 최대한 수행한 뒤 정확히 아래 한 줄만 출력한다.
계속 진행 필요
EOF

  echo "=== Codex iteration ${iteration}/${MAX_ITERATIONS} start ($(date '+%Y-%m-%d %H:%M:%S %z')) ==="
  set +e
  codex "${CODEX_ARGS[@]}" "${MODEL_ARG[@]}" \
    -o "${last_message}" \
    - < "${prompt_file}" 2>&1 | tee "${run_log}"
  codex_rc=${PIPESTATUS[0]}
  set -e

  if [[ "${codex_rc}" -ne 0 ]]; then
    echo "Codex exec failed on iteration ${iteration} with exit code ${codex_rc}." >&2
    echo "Run log: ${run_log}" >&2
    exit "${codex_rc}"
  fi

  if [[ ! -f "${last_message}" ]]; then
    echo "Codex did not write the final message file: ${last_message}" >&2
    exit 1
  fi

  final_message="$(tr -d '\r' < "${last_message}")"
  echo "=== Codex iteration ${iteration} final message ==="
  printf '%s\n' "${final_message}"

  if [[ "${final_message}" == "미적용 사항이 없습니다." ]]; then
    echo "=== Codex execution guide loop complete ==="
    exit 0
  fi

  if [[ "${final_message}" == 사용자\ 입력\ 필요:* ]]; then
    echo "=== Codex execution guide loop blocked ==="
    exit 2
  fi

  if [[ "${final_message}" != "계속 진행 필요" ]]; then
    echo "Unexpected Codex final message on iteration ${iteration}." >&2
    echo "Expected one of: 미적용 사항이 없습니다. / 계속 진행 필요 / 사용자 입력 필요: ..." >&2
    echo "Run log: ${run_log}" >&2
    exit 1
  fi

  iteration=$((iteration + 1))
done

echo "Reached max iterations without completion: ${MAX_ITERATIONS}" >&2
exit 3
