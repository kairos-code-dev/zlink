#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

is_uint() {
  [[ "${1:-}" =~ ^[0-9]+$ ]]
}

clients="${PERF_MULTI_CLIENTS:-100}"
if ! is_uint "${clients}"; then
  clients="100"
fi

if [[ "${PERF_SKIP_NOFILE_CHECK:-0}" != "1" ]]; then
  required=$(( clients * 3 + 4096 ))
  soft="$(ulimit -Sn 2>/dev/null || true)"
  hard="$(ulimit -Hn 2>/dev/null || true)"
  if [[ "${soft}" != "" && "${soft}" != "unlimited" ]] && is_uint "${soft}"; then
    soft_num="${soft}"
    hard_num="${soft_num}"
    if [[ "${hard}" == "unlimited" ]]; then
      hard_num=$(( required > soft_num ? required : soft_num ))
    elif is_uint "${hard}"; then
      hard_num="${hard}"
    fi

    if (( soft_num < required )); then
      target="${required}"
      if (( target > hard_num )); then
        target="${hard_num}"
      fi
      if (( target > soft_num )); then
        ulimit -Sn "${target}" 2>/dev/null || true
        soft="$(ulimit -Sn 2>/dev/null || true)"
        if is_uint "${soft}"; then
          soft_num="${soft}"
        fi
      fi
    fi

    if (( soft_num < required )); then
      echo "Error: nofile soft limit too low (soft=${soft_num}, required=${required})." >&2
      echo "Hint: export PERF_SKIP_NOFILE_CHECK=1 to bypass preflight." >&2
      exit 1
    fi
  fi
fi

exec env PERF_MULTI_CLIENTS="${clients}" "${ROOT_DIR}/bindings/perf/run_policy_bench.py" --binding java --suite multi "$@"
