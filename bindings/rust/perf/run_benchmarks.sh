#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_DIR="$(cd "${PROJECT_DIR}/../.." && pwd)"
CORE_BUILD_DIR="${REPO_DIR}/core/build"
CORE_LIB_DIR="${CORE_BUILD_DIR}/lib"
CORE_LIB="${CORE_LIB_DIR}/libzlink.so"

source "$HOME/.cargo/env" 2>/dev/null || true
export RUSTFLAGS="${RUSTFLAGS:+${RUSTFLAGS} }-Awarnings"

# -- Defaults (matching core/perf) -------------------------------------------
PATTERN="ALL"
DURATION="${PERF_SINGLE_DURATION_SECONDS:-5}"
MSG_SIZES="${PERF_MSG_SIZES:-64,256,1024,65536,131072,262144}"
TRANSPORTS="${PERF_TRANSPORTS:-}"
RUNS="${PERF_RUNS:-1}"
RESULTS_ROOT="${PERF_RESULTS_DIR:-${SCRIPT_DIR}/results}"
RESULTS_TAG="${PERF_RESULTS_TAG:-}"
OUTPUT_FILE=""
REUSE_BUILD=0
CLEAN_BUILD=0
PIN_CPU=0
BUILD_DIR=""
IO_THREADS="${PERF_IO_THREADS:-}"
HWM=""
SEND_HWM=""
RECV_HWM=""
SNDBUF=""
RCVBUF=""
SNDTIMEO_MS="${PERF_SINGLE_SNDTIMEO_MS:-200}"
RCVTIMEO_MS="${PERF_SINGLE_RCVTIMEO_MS:-200}"
AUTO_HWM_PROFILE="${PERF_CTX_AUTO_HWM_PROFILE:-}"
RUN_COOLDOWN_MS="${PERF_SINGLE_RUN_COOLDOWN_MS:-500}"
CASE_RETRIES="${PERF_SINGLE_CASE_RETRIES:-3}"

print_help() {
    cat <<'EOF'
Usage: bindings/rust/perf/run_benchmarks.sh [options]

Options:
  -h, --help
  --pattern NAME
  --duration N
  --msg-sizes LIST
  --transports LIST
  --runs N
  --build-dir PATH
  --reuse-build
  --clean-build
  --pin-cpu
  --io-threads N
  --hwm N
  --send-hwm N
  --recv-hwm N
  --buf SIZE
  --sndbuf SIZE
  --rcvbuf SIZE
  --sndtimeo N
  --rcvtimeo N
  --send-timeout-ms N
  --recv-timeout-ms N
  --auto-hwm-profile NAME
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
        --duration)  DURATION="$2";  shift 2 ;;
        --msg-sizes) MSG_SIZES="$2"; shift 2 ;;
        --transports) TRANSPORTS="$2"; shift 2 ;;
        --runs)      RUNS="$2";      shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --io-threads) IO_THREADS="$2"; shift 2 ;;
        --hwm) HWM="$2"; shift 2 ;;
        --send-hwm) SEND_HWM="$2"; shift 2 ;;
        --recv-hwm) RECV_HWM="$2"; shift 2 ;;
        --buf) SNDBUF="$2"; RCVBUF="$2"; shift 2 ;;
        --sndbuf) SNDBUF="$2"; shift 2 ;;
        --rcvbuf) RCVBUF="$2"; shift 2 ;;
        --sndtimeo|--send-timeout-ms) SNDTIMEO_MS="$2"; shift 2 ;;
        --rcvtimeo|--recv-timeout-ms) RCVTIMEO_MS="$2"; shift 2 ;;
        --auto-hwm-profile) AUTO_HWM_PROFILE="$2"; shift 2 ;;
        --results-dir) RESULTS_ROOT="$2"; shift 2 ;;
        --results-tag) RESULTS_TAG="$2"; shift 2 ;;
        --output)    OUTPUT_FILE="$2"; shift 2 ;;
        --reuse-build) REUSE_BUILD=1; shift ;;
        --clean-build) CLEAN_BUILD=1; shift ;;
        --pin-cpu) PIN_CPU=1; shift ;;
        *)           echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

if [[ "${REUSE_BUILD}" -eq 1 && "${CLEAN_BUILD}" -eq 1 ]]; then
    echo "--reuse-build and --clean-build are mutually exclusive" >&2
    exit 1
fi

# -- Platform ----------------------------------------------------------------
case "$(uname -s)" in
    Linux*)  PLATFORM="linux" ;;
    Darwin*) PLATFORM="macos" ;;
    MINGW*|MSYS*|CYGWIN*) PLATFORM="windows" ;;
    *)       PLATFORM="linux" ;;
esac

default_transports_for_pattern() {
    local pattern="$1"
    case "${pattern}" in
        SPOT)
            printf '%s' "tcp,tls,ws,wss"
            ;;
        *)
            if [[ "${PLATFORM}" == "windows" ]]; then
                printf '%s' "tcp,tls,ws,wss,inproc"
            else
                printf '%s' "tcp,tls,ws,wss,inproc,ipc"
            fi
            ;;
    esac
}

prepare_core_runtime() {
    if [[ ! -f "${CORE_LIB}" ]]; then
        echo "core runtime not found: ${CORE_LIB}" >&2
        echo "Build core/build before running Rust perf." >&2
        exit 1
    fi
    local newer_source
    newer_source="$(
        find "${REPO_DIR}/core/src" "${REPO_DIR}/core/include" \
            -type f -newer "${CORE_LIB}" -print -quit 2>/dev/null || true
    )"
    if [[ -n "${newer_source}" ]]; then
        echo "Error: stale core runtime detected for bindings/rust/perf." >&2
        echo "  runtime: ${CORE_LIB}" >&2
        echo "  newer source: ${newer_source}" >&2
        echo "Rebuild core/build before running run_benchmarks.sh." >&2
        exit 1
    fi
    echo "Perf core build dir: ${CORE_BUILD_DIR}"
    echo "Perf runtime libzlink: ${CORE_LIB}"
    export LD_LIBRARY_PATH="${CORE_LIB_DIR}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
}

TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
TAG_SUFFIX=""
if [[ -n "${RESULTS_TAG}" ]]; then
    TAG_SUFFIX="_${RESULTS_TAG}"
fi
REPORT_DIR="${RESULTS_ROOT}/single/report"
RESULTS_FILE="${REPORT_DIR}/perf_rust_single_${PLATFORM}_${TIMESTAMP}${TAG_SUFFIX}.txt"
mkdir -p "${REPORT_DIR}"

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

sleep_millis() {
    local millis="${1:-0}"
    if [[ ! "${millis}" =~ ^[0-9]+$ || "${millis}" -eq 0 ]]; then
        return
    fi
    sleep "$((millis / 1000)).$(printf '%03d' "$((millis % 1000))")"
}

# -- Build -------------------------------------------------------------------
TARGET_DIR="${BUILD_DIR:-${SCRIPT_DIR}/single/target}"
SINGLE_DIR="${TARGET_DIR}/release"

if [[ "${REUSE_BUILD}" -eq 0 ]]; then
    if [[ "${CLEAN_BUILD}" -eq 1 ]]; then
        (cd "${SCRIPT_DIR}/single" && CARGO_TARGET_DIR="${TARGET_DIR}" cargo clean --quiet)
    fi
    (cd "${SCRIPT_DIR}/single" && CARGO_TARGET_DIR="${TARGET_DIR}" cargo build --release --quiet)
elif [[ ! -x "${SINGLE_DIR}/perf_pair" ]]; then
    echo "existing single perf binaries not found for --reuse-build: ${SINGLE_DIR}" >&2
    exit 1
fi

prepare_core_runtime
[[ -n "${IO_THREADS}" ]] && export PERF_IO_THREADS="${IO_THREADS}"
if [[ -n "${SEND_HWM}" ]]; then
    export PERF_SINGLE_SNDHWM="${SEND_HWM}"
elif [[ -n "${HWM}" ]]; then
    export PERF_SINGLE_SNDHWM="${HWM}"
fi
if [[ -n "${RECV_HWM}" ]]; then
    export PERF_SINGLE_RCVHWM="${RECV_HWM}"
elif [[ -n "${HWM}" ]]; then
    export PERF_SINGLE_RCVHWM="${HWM}"
fi
[[ -n "${SNDBUF}" ]] && export PERF_SINGLE_SNDBUF="${SNDBUF}"
[[ -n "${RCVBUF}" ]] && export PERF_SINGLE_RCVBUF="${RCVBUF}"
export PERF_SINGLE_SNDTIMEO_MS="${SNDTIMEO_MS}"
export PERF_SINGLE_RCVTIMEO_MS="${RCVTIMEO_MS}"
[[ -n "${AUTO_HWM_PROFILE}" ]] && export PERF_CTX_AUTO_HWM_PROFILE="${AUTO_HWM_PROFILE}"

RUN_PREFIX=()
if [[ "${PIN_CPU}" -eq 1 ]]; then
    if [[ "$(uname -s)" != "Linux" ]]; then
        echo "--pin-cpu is only supported on Linux in this runner" >&2
        exit 1
    fi
    if ! command -v taskset >/dev/null 2>&1; then
        echo "--pin-cpu requires taskset" >&2
        exit 1
    fi
    RUN_PREFIX=("taskset" "-c" "0")
fi

IFS=',' read -ra SIZE_LIST <<< "${MSG_SIZES}"

if [[ "${PATTERN}" == "ALL" ]]; then
    PATTERNS=("PAIR" "PUBSUB" "DEALER_DEALER" "DEALER_ROUTER" "ROUTER_ROUTER" "SPOT")
else
    IFS=',' read -ra PATTERNS <<< "${PATTERN}"
fi

TMP_METRICS="$(mktemp)"
TMP_CASES="$(mktemp)"
trap 'rm -f "${TMP_METRICS}" "${TMP_CASES}"' EXIT
METRICS_REGEX='^(throughput|bandwidth|latency|latency_p95|latency_p99)$'
BIN_TIMEOUT_SECONDS="${PERF_SINGLE_TIMEOUT_SECONDS:-$(( DURATION * 6 + 15 ))}"
if [[ "${BIN_TIMEOUT_SECONDS}" -lt 30 ]]; then
    BIN_TIMEOUT_SECONDS=30
fi

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
    current_transports="${TRANSPORTS:-$(default_transports_for_pattern "${pat}")}"
    IFS=',' read -ra TRANSPORT_LIST <<< "${current_transports}"
    for transport in "${TRANSPORT_LIST[@]}"; do
        for size in "${SIZE_LIST[@]}"; do
            case_status="success"
            case_reason=""
            for run in $(seq 1 "${RUNS}"); do
                attempt=1
                while true; do
                    if OUTPUT="$(timeout "${BIN_TIMEOUT_SECONDS}s" "${RUN_PREFIX[@]}" "${BIN}" \
                        --pattern "${pat}" \
                        --transport "${transport}" \
                        --msg-size "${size}" \
                        --duration "${DURATION}" 2>&1)"; then
                        break
                    fi
                    if [[ "${attempt}" -ge "${CASE_RETRIES}" ]]; then
                        case_status="fail"
                        case_reason="binary_exit after ${attempt} attempts"
                        break
                    fi
                    attempt=$((attempt + 1))
                    sleep_millis "${RUN_COOLDOWN_MS}"
                done
                if [[ "${case_status}" == "fail" ]]; then
                    break
                fi
                unsupported_line="$(printf '%s\n' "${OUTPUT}" | awk -F',' '/^UNSUPPORTED,/ {print; exit}')"
                if [[ -n "${unsupported_line}" ]]; then
                    case_status="unsupported"
                    case_reason="${unsupported_line}"
                    break
                fi
                REQUIRED_COUNT="$(printf '%s\n' "${OUTPUT}" | awk -F',' '/^RESULT,/ && ($6=="throughput" || $6=="bandwidth" || $6=="latency" || $6=="latency_p95" || $6=="latency_p99") {count++} END {print count+0}')"
                if [[ "${REQUIRED_COUNT}" -ne 5 ]]; then
                    case_status="fail"
                    case_reason="missing_required_result_lines run=${run}"
                    break
                fi
                while IFS= read -r line; do
                    [[ "${line}" == RESULT,* ]] || continue
                    IFS=',' read -r tag lib result_pattern result_transport result_size metric value <<< "${line}"
                    [[ "${metric}" =~ ${METRICS_REGEX} ]] || continue
                    printf '%s,%s,%s,%s,%s,%s\n' \
                        "${pat}" "${transport}" "${size}" "${run}" "${metric}" "${value}" >> "${TMP_METRICS}"
                done <<< "${OUTPUT}"
            done
            case_reason="${case_reason//,/;}"
            printf '%s,%s,%s,%s,%s\n' "${pat}" "${transport}" "${size}" "${case_status}" "${case_reason}" >> "${TMP_CASES}"
            sleep_millis "${RUN_COOLDOWN_MS}"
        done
    done
done

python3 - "${TMP_METRICS}" "${TMP_CASES}" "${RESULTS_FILE}" "${PATTERN}" "${TRANSPORTS}" "${MSG_SIZES}" \
  "${RUNS}" "${DURATION}" "${RESULTS_TAG}" "${OUTPUT_FILE}" "${PIN_CPU}" "${IO_THREADS}" \
  "${HWM}" "${SEND_HWM}" "${RECV_HWM}" "${SNDTIMEO_MS}" "${RCVTIMEO_MS}" "${SECONDS}" <<'PY'
import csv
import math
import sys
from collections import defaultdict

metrics_path, cases_path, report_path, pattern_csv, transports_csv, msg_sizes_csv, runs, duration, results_tag, output_path, pin_cpu, io_threads, hwm, send_hwm, recv_hwm, sndtimeo_ms, rcvtimeo_ms, elapsed_seconds = sys.argv[1:]
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
    return "N/A" if math.isnan(value) else f"{value / 1000.0:.3f}"

def fmt_bandwidth(value):
    return "N/A" if math.isnan(value) else f"{value:.3f} MB/s"

def fmt_latency_ms(value):
    return "N/A" if math.isnan(value) else f"{value:.3f} ms"

def fmt_metric(value):
    return "N/A" if math.isnan(value) else f"{value:.3f}"

expected = 0
actual = 0
lines = []
result_lines = []
failures = []

def emit(line=""):
    lines.append(line)

def emit_effective_options(section):
    emit(f"## Effective Options ({section})")
    emit("- lang: rust")
    emit("- suite: single")
    emit(f"- runs: {runs}")
    emit(f"- duration_seconds: {duration}")
    emit("- timeout_seconds: 30")
    emit(f"- io_threads: {io_threads or 'default(binary=1)'}")
    emit(f"- hwm: {hwm or 'auto-hwm'}")
    emit(f"- sndhwm: {send_hwm or hwm or 'auto-hwm'}")
    emit(f"- rcvhwm: {recv_hwm or hwm or 'auto-hwm'}")
    emit("- sndbuf: auto-hwm")
    emit("- rcvbuf: auto-hwm")
    emit(f"- sndtimeo_ms: {sndtimeo_ms}")
    emit(f"- rcvtimeo_ms: {rcvtimeo_ms}")
    emit("- ctx_auto_hwm_enable: core-default")
    emit("- ctx_auto_hwm_profile: balanced")
    emit(f"- patterns: {pattern_csv}")
    emit(f"- transports: {transports_csv}")
    emit(f"- msg_sizes: {msg_sizes_csv}")
    emit("")

emit_effective_options("start")

def pattern_direction(pattern):
    return "one-way"

def rate_unit(pattern):
    return "Kops/s" if pattern_direction(pattern) == "echo" else "Kmsg/s"

def emit_table_header():
    emit("| Size     |         Throughput |      Bandwidth |  Lat.Mean(ms) |   Lat.P95(ms) |   Lat.P99(ms) |")
    emit("|----------|--------------------|----------------|---------------|---------------|---------------|")

def metric_value_for_run(key, metric, run_index):
    values = rows[key].get(metric, [])
    if run_index < len(values):
        return values[run_index]
    return math.nan

def emit_case_row(pattern, size, metric_values):
    throughput = f"{fmt_rate(metric_values['throughput']):>8} {rate_unit(pattern)}"
    emit(
        f"      | {str(size) + 'B':<8} | {throughput:>16} | "
        f"{fmt_bandwidth(metric_values['bandwidth']):>12} | "
        f"{fmt_latency_ms(metric_values['latency']):>12} | "
        f"{fmt_latency_ms(metric_values['latency_p95']):>12} | "
        f"{fmt_latency_ms(metric_values['latency_p99']):>12} |"
    )

for pattern_index, pattern in enumerate(patterns):
    if pattern_index:
        emit("===============================================================================")
        emit("")
    emit(f"## PATTERN: {pattern} ({pattern_direction(pattern)})")
    emit(f"  > Benchmarking current for {pattern}...")
    for transport in pattern_transports[pattern]:
        emit(f"    Testing {transport}:")
        if runs > 1:
            for run_index in range(runs):
                emit(f"      run {run_index + 1}/{runs}:")
                for header in ("| Size     |         Throughput |      Bandwidth |  Lat.Mean(ms) |   Lat.P95(ms) |   Lat.P99(ms) |", "|----------|--------------------|----------------|---------------|---------------|---------------|"):
                    emit(f"        {header}")
                for size in pattern_sizes[pattern]:
                    key = (pattern, transport, size)
                    status, _reason = cases.get(key, ("fail", "missing_case_status"))
                    if status == "unsupported":
                        emit(
                            f"        | {size}B | {status.upper()} | {status.upper()} | "
                            f"{status.upper()} | {status.upper()} | {status.upper()} |"
                        )
                        continue
                    metric_values = {
                        metric: metric_value_for_run(key, metric, run_index)
                        for metric in required_metrics
                    }
                    if any(math.isnan(value) for value in metric_values.values()):
                        emit(f"        | {size}B | FAIL | FAIL | FAIL | FAIL | FAIL |")
                    else:
                        emit_case_row(pattern, size, metric_values)
            emit("      median:")
        for header in ("| Size     |         Throughput |      Bandwidth |  Lat.Mean(ms) |   Lat.P95(ms) |   Lat.P99(ms) |", "|----------|--------------------|----------------|---------------|---------------|---------------|"):
            emit(f"      {header}")
        for size in pattern_sizes[pattern]:
            key = (pattern, transport, size)
            status, reason = cases.get(key, ("fail", "missing_case_status"))
            if status == "unsupported":
                emit(
                    f"      | {size}B | {status.upper()} | {status.upper()} | "
                    f"{status.upper()} | {status.upper()} | {status.upper()} |"
                )
                continue

            expected += 5
            metric_values = {metric: median(rows[key].get(metric, [])) for metric in required_metrics}
            complete_case = all(len(rows[key].get(metric, [])) == runs for metric in required_metrics)
            if complete_case:
                actual += 5
                emit_case_row(pattern, size, metric_values)
                for metric in required_metrics:
                    result_lines.append(
                        f"RESULT,current,{pattern},{transport},{size},{metric},{fmt_metric(metric_values[metric])}"
                    )
            else:
                emit("      | {}B | FAIL | FAIL | FAIL | FAIL | FAIL |".format(size))
                failures.append(f"{pattern} current {transport} {size}B: {reason or 'missing_result_lines'}")
        emit(f"    Testing {transport}: Done")
    emit("")

emit("## Auto-HWM Detail")
emit("- unavailable: binding runner does not expose socket-level Auto-HWM metadata")
emit("")
emit_effective_options("result")
emit("## Result Data")
for line in result_lines:
    emit(line)
emit("")
emit("## Completion")
emit(f"- status: {'complete' if expected == actual else 'partial'}")
emit(f"- expected_result_lines: {expected}")
emit(f"- actual_result_lines: {actual}")
if failures:
    emit("")
    emit("## Failures")
    for failure in failures:
        emit(f"- {failure}")
status = 'complete' if expected == actual else 'partial'
emit("")
emit(f"Saved result file: {report_path} (status={status})")
emit(f"Total benchmark time: {elapsed_seconds}s ({elapsed_seconds}s, exit={0 if status == 'complete' else 1})")

text = "\n".join(lines) + "\n"
with open(report_path, "w", encoding="utf-8") as f:
    f.write(text)
if output_path:
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(text)
sys.stdout.write(text)
sys.exit(0 if expected == actual else 1)
PY

prune_reports "${REPORT_DIR}"
