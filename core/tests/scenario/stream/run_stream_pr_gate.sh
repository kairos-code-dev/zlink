#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"
COMPARE_SH="${SCRIPT_DIR}/run_stream_compare.sh"

BASELINE_ROOT="${BASELINE_ROOT:-${ROOT_DIR}/doc/plan/baseline}"
RESULT_ROOT="${RESULT_ROOT:-${SCRIPT_DIR}/result/pr_gate_$(date +%Y%m%d_%H%M%S)}"

CCU=1000
DURATION=5
REPEATS=3
INFLIGHT=1
CLIENT_IO_THREADS=1
SERVER_IO_THREADS=1

THROUGHPUT_MIN_RATIO="${THROUGHPUT_MIN_RATIO:-0.97}"
P99_MAX_RATIO="${P99_MAX_RATIO:-1.05}"

usage()
{
    cat <<'USAGE'
Usage:
  run_stream_pr_gate.sh [options]

Options:
  --baseline-root <dir>         baseline root (default: doc/plan/baseline)
  --result-root <dir>           output root
  --ccu <N>                     client connections (default: 1000)
  --duration <sec>              duration seconds (default: 5)
  --repeats <N>                 repeats (default: 3)
  --inflight <N>                inflight per conn (default: 1)
  --client-io-threads <N>       client io threads (default: 1)
  --server-io-threads <N>       server io threads (default: 1)
  --throughput-min-ratio <F>    current/base minimum (default: 0.97)
  --p99-max-ratio <F>           current/base maximum (default: 1.05)
  -h, --help

Runs zlink STREAM PR performance gate for size 64 and 1024.
USAGE
}

validate_int()
{
    local name="$1"
    local value="$2"
    if ! [[ "${value}" =~ ^[0-9]+$ ]] || (( value < 1 )); then
        echo "invalid ${name}: ${value}" >&2
        exit 2
    fi
}

validate_float()
{
    local name="$1"
    local value="$2"
    if ! [[ "${value}" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
        echo "invalid ${name}: ${value}" >&2
        exit 2
    fi
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --baseline-root)
            BASELINE_ROOT="${2:-}"
            shift 2
            ;;
        --result-root)
            RESULT_ROOT="${2:-}"
            shift 2
            ;;
        --ccu)
            CCU="${2:-}"
            shift 2
            ;;
        --duration)
            DURATION="${2:-}"
            shift 2
            ;;
        --repeats)
            REPEATS="${2:-}"
            shift 2
            ;;
        --inflight)
            INFLIGHT="${2:-}"
            shift 2
            ;;
        --client-io-threads)
            CLIENT_IO_THREADS="${2:-}"
            shift 2
            ;;
        --server-io-threads)
            SERVER_IO_THREADS="${2:-}"
            shift 2
            ;;
        --throughput-min-ratio)
            THROUGHPUT_MIN_RATIO="${2:-}"
            shift 2
            ;;
        --p99-max-ratio)
            P99_MAX_RATIO="${2:-}"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

validate_int "--ccu" "${CCU}"
validate_int "--duration" "${DURATION}"
validate_int "--repeats" "${REPEATS}"
validate_int "--inflight" "${INFLIGHT}"
validate_int "--client-io-threads" "${CLIENT_IO_THREADS}"
validate_int "--server-io-threads" "${SERVER_IO_THREADS}"
validate_float "--throughput-min-ratio" "${THROUGHPUT_MIN_RATIO}"
validate_float "--p99-max-ratio" "${P99_MAX_RATIO}"

mkdir -p "${RESULT_ROOT}"

run_and_check()
{
    local size="$1"
    local baseline_summary="${BASELINE_ROOT}/20260217_phase0_zlink${size}/summary.json"
    local run_dir="${RESULT_ROOT}/zlink_s${size}"
    local current_summary="${run_dir}/summary.json"

    if [[ ! -f "${baseline_summary}" ]]; then
        echo "baseline summary not found: ${baseline_summary}" >&2
        return 2
    fi

    echo "[PR-GATE] run size=${size} result_dir=${run_dir}"
    RESULT_DIR="${run_dir}" "${COMPARE_SH}" \
        --stack zlink \
        --size "${size}" \
        --ccu "${CCU}" \
        --duration "${DURATION}" \
        --repeats "${REPEATS}" \
        --inflight "${INFLIGHT}" \
        --client-io-threads "${CLIENT_IO_THREADS}" \
        --server-io-threads "${SERVER_IO_THREADS}"

    python3 - "${baseline_summary}" "${current_summary}" "${size}" \
        "${THROUGHPUT_MIN_RATIO}" "${P99_MAX_RATIO}" <<'PY'
import json
import sys

base_path, cur_path, size_s, min_tp_s, max_p99_s = sys.argv[1:6]
size_key = str(int(size_s))
min_tp = float(min_tp_s)
max_p99 = float(max_p99_s)

with open(base_path, "r", encoding="utf-8") as f:
    base = json.load(f)
with open(cur_path, "r", encoding="utf-8") as f:
    cur = json.load(f)

def must_metrics(obj, label):
    med = obj.get("medians", {}).get(size_key, {}).get("zlink")
    if med is None:
        raise SystemExit(f"{label}: missing medians for size={size_key}")
    if "throughput_msg_s" not in med or "p99_us" not in med:
        raise SystemExit(f"{label}: missing throughput_msg_s/p99_us")
    return float(med["throughput_msg_s"]), float(med["p99_us"])

def must_pass(obj, label):
    if obj.get("row_mismatch", False):
        raise SystemExit(f"{label}: row_mismatch=true")
    if int(obj.get("invalid_pass_rows", 0)) > 0:
        raise SystemExit(f"{label}: invalid_pass_rows={obj.get('invalid_pass_rows')}")
    c = obj.get("pass_count", {}).get(size_key, {}).get("zlink")
    if not c:
        raise SystemExit(f"{label}: missing pass_count for size={size_key}")
    total = int(c.get("total", 0))
    passed = int(c.get("pass", 0))
    if total < 1 or passed != total:
        raise SystemExit(f"{label}: pass/total={passed}/{total}")

must_pass(base, "baseline")
must_pass(cur, "current")

base_tp, base_p99 = must_metrics(base, "baseline")
cur_tp, cur_p99 = must_metrics(cur, "current")

if base_tp <= 0 or base_p99 <= 0:
    raise SystemExit("baseline metrics must be > 0")

tp_ratio = cur_tp / base_tp
p99_ratio = cur_p99 / base_p99

print(
    f"[PR-GATE] size={size_key} "
    f"throughput_ratio={tp_ratio:.4f} "
    f"p99_ratio={p99_ratio:.4f} "
    f"(thresholds: throughput>={min_tp:.3f}, p99<={max_p99:.3f})"
)

if tp_ratio < min_tp:
    raise SystemExit(
        f"throughput regression: size={size_key} ratio={tp_ratio:.4f} < {min_tp:.4f}"
    )
if p99_ratio > max_p99:
    raise SystemExit(
        f"p99 regression: size={size_key} ratio={p99_ratio:.4f} > {max_p99:.4f}"
    )
PY
}

run_and_check 64
run_and_check 1024

echo "[PR-GATE] PASS result_root=${RESULT_ROOT}"
