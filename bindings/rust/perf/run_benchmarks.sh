#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

source "$HOME/.cargo/env" 2>/dev/null || true

# -- Defaults (matching core/perf) -------------------------------------------
PATTERN="ALL"
RECV_MODE="callback"
DURATION="${PERF_SINGLE_DURATION_SECONDS:-5}"
MSG_SIZES="${PERF_MSG_SIZES:-64,256,1024,65536,131072,262144}"
TRANSPORTS="${PERF_TRANSPORTS:-tcp}"
RUNS="${PERF_RUNS:-1}"
RESULTS_DIR="${PERF_RESULTS_DIR:-${SCRIPT_DIR}/results/single/report}"
RESULTS_TAG="${PERF_RESULTS_TAG:-}"
OUTPUT_FILE=""

print_help() {
    cat <<'EOF'
Usage: bindings/rust/perf/run_benchmarks.sh [options]

Options:
  -h, --help
  --pattern NAME
  --recv MODE
  --duration N
  --msg-sizes LIST
  --transports LIST
  --runs N
  --results-dir PATH
  --results-tag NAME
  --output PATH
EOF
}

# -- Parse CLI options -------------------------------------------------------
while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            print_help
            exit 0
            ;;
        --pattern)   PATTERN="$2";   shift 2 ;;
        --recv)      RECV_MODE="$2"; shift 2 ;;
        --duration)  DURATION="$2";  shift 2 ;;
        --msg-sizes) MSG_SIZES="$2"; shift 2 ;;
        --transports) TRANSPORTS="$2"; shift 2 ;;
        --runs)      RUNS="$2";      shift 2 ;;
        --results-dir) RESULTS_DIR="$2"; shift 2 ;;
        --results-tag) RESULTS_TAG="$2"; shift 2 ;;
        --output)    OUTPUT_FILE="$2"; shift 2 ;;
        --build-dir|--io-threads|--hwm|--send-hwm|--recv-hwm) shift 2 ;;
        --reuse-build|--clean-build|--pin-cpu) shift ;;
        *)           echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

if [[ "${RECV_MODE}" != "callback" ]]; then
    echo "single suite supports only --recv callback" >&2
    exit 1
fi

# -- Platform ----------------------------------------------------------------
case "$(uname -s)" in
    Linux*)  PLATFORM="linux" ;;
    Darwin*) PLATFORM="macos" ;;
    MINGW*|MSYS*|CYGWIN*) PLATFORM="windows" ;;
    *)       PLATFORM="linux" ;;
esac

TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
TAG_SUFFIX=""
if [[ -n "${RESULTS_TAG}" ]]; then
    TAG_SUFFIX="_${RESULTS_TAG}"
fi
RESULTS_FILE="${RESULTS_DIR}/perf_${PLATFORM}_${RECV_MODE}_${TIMESTAMP}${TAG_SUFFIX}.txt"
mkdir -p "${RESULTS_DIR}"

prune_reports() {
    local report_dir="$1"
    local count
    count="$(find "${report_dir}" -maxdepth 1 -type f -name 'perf_*.txt' | wc -l | tr -d ' ')"
    if [[ -z "${count}" || "${count}" -le 100 ]]; then
        return
    fi
    find "${report_dir}" -maxdepth 1 -type f -name 'perf_*.txt' -printf '%f\n' \
        | sort \
        | head -n "$((count - 100))" \
        | while read -r old_file; do
            rm -f "${report_dir}/${old_file}"
        done
}

# -- Build -------------------------------------------------------------------
(cd "${SCRIPT_DIR}/single" && cargo build --release --quiet)

SINGLE_DIR="${SCRIPT_DIR}/single/target/release"

# Native library path
export LD_LIBRARY_PATH="${PROJECT_DIR}/native/linux-x86_64:${LD_LIBRARY_PATH:-}"

IFS=',' read -ra SIZE_LIST <<< "${MSG_SIZES}"
IFS=',' read -ra TRANSPORT_LIST <<< "${TRANSPORTS}"

if [[ "${PATTERN}" == "ALL" ]]; then
    PATTERNS=("PAIR" "PUBSUB" "DEALER_DEALER" "DEALER_ROUTER" "ROUTER_ROUTER" "SPOT")
else
    IFS=',' read -ra PATTERNS <<< "${PATTERN}"
fi

TMP_METRICS="$(mktemp)"
TMP_CASES="$(mktemp)"
trap 'rm -f "${TMP_METRICS}" "${TMP_CASES}"' EXIT
METRICS_REGEX='^(throughput|bandwidth|latency|latency_p95|latency_p99)$'

for pat in "${PATTERNS[@]}"; do
    BIN=""
    case "${pat}" in
        PAIR)            BIN="${SINGLE_DIR}/perf_pair" ;;
        PUBSUB)          BIN="${SINGLE_DIR}/perf_pubsub" ;;
        DEALER_DEALER)   BIN="${SINGLE_DIR}/perf_dealer_dealer" ;;
        DEALER_ROUTER)   BIN="${SINGLE_DIR}/perf_dealer_router" ;;
        ROUTER_ROUTER)   BIN="${SINGLE_DIR}/perf_router_router" ;;
        SPOT)            BIN="${SINGLE_DIR}/perf_spot" ;;
        *)               continue ;;
    esac
    for transport in "${TRANSPORT_LIST[@]}"; do
        for size in "${SIZE_LIST[@]}"; do
            case_status="success"
            case_reason=""
            for run in $(seq 1 "${RUNS}"); do
                if ! OUTPUT="$("${BIN}" \
                    --pattern "${pat}" \
                    --transport "${transport}" \
                    --msg-size "${size}" \
                    --duration "${DURATION}" \
                    --recv "${RECV_MODE}" 2>&1)"; then
                    case_status="fail"
                    case_reason="binary_exit"
                    break
                fi
                unsupported_line="$(printf '%s\n' "${OUTPUT}" | awk -F',' '/^UNSUPPORTED,/ {print; exit}')"
                if [[ -n "${unsupported_line}" ]]; then
                    case_status="unsupported"
                    case_reason="${unsupported_line}"
                    break
                fi
                skip_line="$(printf '%s\n' "${OUTPUT}" | awk -F',' '/^SKIP,/ {print; exit}')"
                if [[ -n "${skip_line}" ]]; then
                    case_status="skip"
                    case_reason="${skip_line}"
                    break
                fi
                while IFS= read -r line; do
                    [[ "${line}" == RESULT,* ]] || continue
                    IFS=',' read -r tag lib result_pattern result_transport result_size metric value <<< "${line}"
                    [[ "${metric}" =~ ${METRICS_REGEX} ]] || continue
                    printf '%s,%s,%s,%s,%s,%s\n' \
                        "${pat}" "${transport}" "${size}" "${run}" "${metric}" "${value}" >> "${TMP_METRICS}"
                done <<< "${OUTPUT}"
                REQUIRED_COUNT="$(printf '%s\n' "${OUTPUT}" | awk -F',' '/^RESULT,/ && ($6=="throughput" || $6=="bandwidth" || $6=="latency" || $6=="latency_p95" || $6=="latency_p99") {count++} END {print count+0}')"
                if [[ "${REQUIRED_COUNT}" -ne 5 ]]; then
                    case_status="fail"
                    case_reason="missing_required_result_lines run=${run}"
                    break
                fi
            done
            printf '%s,%s,%s,%s,%s\n' "${pat}" "${transport}" "${size}" "${case_status}" "${case_reason}" >> "${TMP_CASES}"
        done
    done
done

python3 - "${TMP_METRICS}" "${TMP_CASES}" "${RESULTS_FILE}" "${PATTERN}" "${TRANSPORTS}" "${MSG_SIZES}" \
  "${RECV_MODE}" "${RUNS}" "${DURATION}" "${RESULTS_TAG}" "${OUTPUT_FILE}" <<'PY'
import csv
import math
import sys
from collections import defaultdict

metrics_path, cases_path, report_path, pattern_csv, transports_csv, msg_sizes_csv, recv_mode, runs, duration, results_tag, output_path = sys.argv[1:]
runs = int(runs)
required_metrics = ["throughput", "bandwidth", "latency", "latency_p95", "latency_p99"]
rows = defaultdict(lambda: defaultdict(list))
cases = {}
patterns = []
pattern_transports = defaultdict(list)
pattern_sizes = defaultdict(list)

with open(metrics_path, newline="", encoding="utf-8") as f:
    reader = csv.reader(f)
    for pattern, transport, size, run, metric, value in reader:
        key = (pattern, transport, int(size))
        if pattern not in patterns:
            patterns.append(pattern)
        if transport not in pattern_transports[pattern]:
            pattern_transports[pattern].append(transport)
        if int(size) not in pattern_sizes[pattern]:
            pattern_sizes[pattern].append(int(size))
        try:
            rows[key][metric].append(float(value))
        except ValueError:
            rows[key][metric].append(math.nan)

with open(cases_path, newline="", encoding="utf-8") as f:
    reader = csv.reader(f)
    for pattern, transport, size, status, reason in reader:
        size = int(size)
        cases[(pattern, transport, size)] = (status, reason)
        if pattern not in patterns:
            patterns.append(pattern)
        if transport not in pattern_transports[pattern]:
            pattern_transports[pattern].append(transport)
        if size not in pattern_sizes[pattern]:
            pattern_sizes[pattern].append(size)

for pattern in pattern_sizes:
    pattern_sizes[pattern].sort()

def median(values):
    usable = [v for v in values if not math.isnan(v)]
    if not usable:
        return math.nan
    usable.sort()
    mid = len(usable) // 2
    if len(usable) % 2:
        return usable[mid]
    return (usable[mid - 1] + usable[mid]) / 2.0

def fmt_rate(value):
    return "N/A" if math.isnan(value) else f"{value / 1000.0:.2f} Kmsg/s"

def fmt_bandwidth(value):
    return "N/A" if math.isnan(value) else f"{value:.2f} MB/s"

def fmt_latency_us(value):
    return "N/A" if math.isnan(value) else f"{value:.2f} us"

def fmt_metric(value):
    return "N/A" if math.isnan(value) else f"{value:.2f}"

expected = 0
actual = 0
lines = []
result_lines = []
failures = []

def emit(line=""):
    lines.append(line)

def emit_effective_options(section):
    emit(f"## Effective Options ({section})")
    emit("- suite: single")
    emit(f"- runs: {runs}")
    emit(f"- patterns: {pattern_csv}")
    emit(f"- transports: {transports_csv}")
    emit(f"- msg_sizes: {msg_sizes_csv}")
    emit(f"- recv_mode: {recv_mode}")
    emit("- pin_cpu: off")
    emit(f"- duration_seconds: {duration}")
    if results_tag:
        emit(f"- results_tag: {results_tag}")
    emit("")

emit_effective_options("start")
emit("===============================================================================")
emit("")

for pattern_index, pattern in enumerate(patterns):
    if pattern_index:
        emit("===============================================================================")
        emit("")
    emit(f"## PATTERN: {pattern}")
    emit("")
    for transport in pattern_transports[pattern]:
        emit(f"### Transport: {transport}")
        emit("| Size | Throughput | Bandwidth | Lat.Mean | Lat.P95 | Lat.P99 |")
        emit("|------|------------|-----------|----------|---------|---------|")
        for size in pattern_sizes[pattern]:
            key = (pattern, transport, size)
            status, reason = cases.get(key, ("fail", "missing_case_status"))
            if status in {"unsupported", "skip"}:
                emit(
                    f"| {size}B | {status.upper()} | {status.upper()} | "
                    f"{status.upper()} | {status.upper()} | {status.upper()} |"
                )
                continue

            expected += 5
            metric_values = {metric: median(rows[key].get(metric, [])) for metric in required_metrics}
            complete_case = all(len(rows[key].get(metric, [])) == runs for metric in required_metrics)
            if complete_case:
                actual += 5
                emit(
                    f"| {size}B | {fmt_rate(metric_values['throughput'])} | "
                    f"{fmt_bandwidth(metric_values['bandwidth'])} | "
                    f"{fmt_latency_us(metric_values['latency'])} | "
                    f"{fmt_latency_us(metric_values['latency_p95'])} | "
                    f"{fmt_latency_us(metric_values['latency_p99'])} |"
                )
                for metric in required_metrics:
                    result_lines.append(
                        f"RESULT,current,{pattern},{transport},{size},{metric},{fmt_metric(metric_values[metric])}"
                    )
            else:
                emit("| {}B | FAIL | FAIL | FAIL | FAIL | FAIL |".format(size))
                failures.append(f"{pattern} current {transport} {size}B: {reason or 'missing_result_lines'}")
        emit("")

for line in result_lines:
    emit(line)

emit("")
emit_effective_options("result")
emit("## Completion")
emit(f"- status: {'complete' if expected == actual else 'partial'}")
emit(f"- expected_result_lines: {expected}")
emit(f"- actual_result_lines: {actual}")
emit("")
emit("## Failures")
for failure in failures:
    emit(f"- {failure}")

text = "\n".join(lines) + "\n"
with open(report_path, "w", encoding="utf-8") as f:
    f.write(text)
if output_path:
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(text)
sys.stdout.write(text)
PY

prune_reports "${RESULTS_DIR}"
