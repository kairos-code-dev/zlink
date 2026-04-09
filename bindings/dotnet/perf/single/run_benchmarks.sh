#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOTNET_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PROJECT="${DOTNET_DIR}/perf/single/Zlink.BindingBench/Zlink.BindingBench.csproj"
RESULTS_ROOT="${DOTNET_DIR}/perf/results"
PATTERN="ALL"
TRANSPORTS="${PERF_TRANSPORTS:-}"
MSG_SIZES="${PERF_MSG_SIZES:-64,256,1024,65536,131072,262144}"
WARMUP="${PERF_SINGLE_WARMUP_SECONDS:-2}"
DURATION="${PERF_SINGLE_DURATION_SECONDS:-5}"
RUNS="${PERF_RUNS:-1}"
RESULTS_TAG=""
CONFIGURATION="${PERF_CONFIGURATION:-Release}"
REPORT=""

usage() {
  cat <<'USAGE'
Usage: perf/single/run_benchmarks.sh [options]

Measure current zlink .NET binding single-pattern performance.

Options:
  -h, --help            Show this help.
  --pattern NAME        Pattern list (comma-separated) or ALL.
  --duration N          Active duration seconds (default: 5).
  --warmup N            Warmup duration seconds (default: 2).
  --msg-sizes LIST      Message size list (default: 64,256,1024,65536,131072,262144).
  --transports LIST     Transport list override.
  --runs N              Iterations per configuration (default: 1).
  --results-dir PATH    Override result root directory.
  --results-tag NAME    Optional report suffix tag.

Notes:
  - result is saved under results/single/report/ as
    perf_dotnet_single_<platform>_YYYYMMDD_HHMMSS[_<tag>].txt.
USAGE
}

normalize_platform() {
  case "$(uname -s)" in
    Linux*) printf 'linux' ;;
    Darwin*) printf 'macos' ;;
    MINGW*|MSYS*|CYGWIN*) printf 'windows' ;;
    *) uname -s | tr '[:upper:]' '[:lower:]' ;;
  esac
}

print_line() {
  local line="${1:-}"
  printf '%s\n' "${line}" | tee -a "${REPORT}"
}

validate_uint() {
  local label="${1:-value}"
  local value="${2:-}"
  if [[ ! "${value}" =~ ^[0-9]+$ || "${value}" -lt 1 ]]; then
    echo "${label} must be a positive integer." >&2
    exit 1
  fi
}

normalize_pattern_csv() {
  local raw="${1:-}"
  if [[ "${raw}" == "ALL" ]]; then
    printf '%s' "PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER,SPOT"
    return
  fi

  python3 - "${raw}" <<'PY'
import sys

allowed = {
    "PAIR",
    "PUBSUB",
    "DEALER_DEALER",
    "DEALER_ROUTER",
    "ROUTER_ROUTER",
    "SPOT",
}

items = []
for token in sys.argv[1].split(","):
    value = token.strip().upper()
    if not value:
        continue
    if value not in allowed:
        raise SystemExit(f"unsupported single pattern: {value}")
    items.append(value)

if not items:
    raise SystemExit("no valid single pattern specified")

print(",".join(items))
PY
}

emit_markdown_table() {
  local metrics_file="${1:-}"
  local pattern="${2:-}"
  local transport="${3:-}"
  local run_index="${4:-1}"

  if [[ ! -s "${metrics_file}" ]]; then
    return
  fi

  print_line "## PATTERN: ${pattern} (one-way)"
  print_line "  > Benchmarking current for ${pattern}..."
  print_line "    Testing ${transport}:"
  python3 - "${metrics_file}" <<'PY' | while IFS= read -r table_line; do
import csv
import sys
from collections import OrderedDict

required = [
    "throughput",
    "bandwidth",
    "latency",
    "latency_p95",
    "latency_p99",
]
rows = OrderedDict()
with open(sys.argv[1], encoding="utf-8") as handle:
    reader = csv.reader(handle)
    for row in reader:
        if len(row) != 7 or row[0] != "RESULT":
            continue
        size = row[4]
        metric = row[5]
        rows.setdefault(size, {})
        rows[size][metric] = row[6]

print("      | Size     |       Throughput |   Bandwidth |   Lat.Mean |    Lat.P95 |    Lat.P99 |")
print("      |----------|------------------|-------------|------------|------------|------------|")
for size, metrics in rows.items():
    values = [metrics.get(metric, "NA") for metric in required]
    throughput = float(values[0]) / 1000.0
    bandwidth = float(values[1])
    latency = float(values[2])
    latency_p95 = float(values[3])
    latency_p99 = float(values[4])
    print(
        f"      | {size}B | {throughput:>16.2f} Kmsg/s | {bandwidth:>10.2f} MB/s |"
        f" {latency:>10.2f} us | {latency_p95:>10.2f} us | {latency_p99:>10.2f} us |"
    )
PY
    print_line "${table_line}"
  done
  print_line "    Testing ${transport}: Done"
}

extract_required_results() {
  local log_path="${1:-}"
  local pattern="${2:-}"
  local transport="${3:-}"
  local size="${4:-}"

  python3 - "${log_path}" "${pattern}" "${transport}" "${size}" <<'PY'
import csv
import sys

required = ["throughput", "bandwidth", "latency", "latency_p95", "latency_p99"]
found = {}
with open(sys.argv[1], encoding="utf-8", errors="replace") as handle:
    reader = csv.reader(handle)
    for row in reader:
        if len(row) != 7:
            continue
        if row[0] != "RESULT" or row[1] != "current":
            continue
        if row[2] != sys.argv[2] or row[3] != sys.argv[3] or row[4] != sys.argv[4]:
            continue
        if row[5] in required:
            found[row[5]] = row

missing = [metric for metric in required if metric not in found]
if missing:
    raise SystemExit("missing required metrics: " + ",".join(missing))

for metric in required:
    print(",".join(found[metric]))
PY
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --pattern)
      PATTERN="${2:-}"
      shift
      ;;
    --transports)
      TRANSPORTS="${2:-}"
      shift
      ;;
    --msg-sizes)
      MSG_SIZES="${2:-}"
      shift
      ;;
    --warmup)
      WARMUP="${2:-}"
      shift
      ;;
    --duration)
      DURATION="${2:-}"
      shift
      ;;
    --runs)
      RUNS="${2:-}"
      shift
      ;;
    --results-dir)
      RESULTS_ROOT="${2:-}"
      shift
      ;;
    --results-tag)
      RESULTS_TAG="${2:-}"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
  shift
done

validate_uint "--warmup" "${WARMUP}"
validate_uint "--duration" "${DURATION}"
validate_uint "--runs" "${RUNS}"

if [[ ! "${MSG_SIZES}" =~ ^[0-9]+(,[0-9]+)*$ ]]; then
  echo "--msg-sizes must be a comma-separated list of positive integers." >&2
  exit 1
fi

if [[ -n "${TRANSPORTS}" && ! "${TRANSPORTS}" =~ ^[a-z]+(,[a-z]+)*$ ]]; then
  echo "--transports must be a comma-separated list of transport names." >&2
  exit 1
fi

PATTERN="$(normalize_pattern_csv "${PATTERN}")"

if [[ -z "${TRANSPORTS}" ]]; then
  if [[ "$(uname -s)" == "Linux" ]]; then
    TRANSPORTS="inproc,tcp,ipc"
  else
    TRANSPORTS="inproc,tcp"
  fi
fi

mkdir -p "${RESULTS_ROOT}/single/tmp" "${RESULTS_ROOT}/single/report" \
  "${RESULTS_ROOT}/single/baseline"

platform="$(normalize_platform)"
timestamp="$(date +%Y%m%d_%H%M%S)"
report_base="perf_dotnet_single_${platform}_${timestamp}"
if [[ -n "${RESULTS_TAG}" ]]; then
  report_base="${report_base}_${RESULTS_TAG}"
fi
REPORT="${RESULTS_ROOT}/single/report/${report_base}.txt"
: > "${REPORT}"

print_line "## Effective Options (start)"
print_line "- lang: dotnet"
print_line "- suite: single"
print_line "- runs: ${RUNS}"
print_line "- duration_seconds: ${DURATION}"
print_line "- warmup_seconds: ${WARMUP}"
print_line "- patterns: ${PATTERN}"
print_line "- transports: ${TRANSPORTS}"
print_line "- msg_sizes: ${MSG_SIZES}"
print_line "- pin_cpu: off"
print_line ""

IFS=',' read -r -a patterns <<< "${PATTERN}"
IFS=',' read -r -a transports <<< "${TRANSPORTS}"
IFS=',' read -r -a msg_sizes <<< "${MSG_SIZES}"

status=0
result_lines=0
expected_result_lines=0
for (( run_index=1; run_index<=RUNS; run_index++ )); do
  for pattern in "${patterns[@]}"; do
    pattern="${pattern//[[:space:]]/}"
    [[ -n "${pattern}" ]] || continue

    for transport in "${transports[@]}"; do
      transport="${transport//[[:space:]]/}"
      [[ -n "${transport}" ]] || continue

      metrics_file="${RESULTS_ROOT}/single/tmp/${pattern,,}_${transport}_run${run_index}.metrics"
      : > "${metrics_file}"

      for size in "${msg_sizes[@]}"; do
        size="${size//[[:space:]]/}"
        [[ -n "${size}" ]] || continue
        expected_result_lines=$((expected_result_lines + 5))

        tmp_log="${RESULTS_ROOT}/single/tmp/${pattern,,}_${transport}_${size}_run${run_index}.log"
        echo "RUN pattern=${pattern} transport=${transport} size=${size} run=${run_index}"
        if PERF_SINGLE_WARMUP_SECONDS="${WARMUP}" \
          PERF_SINGLE_DURATION_SECONDS="${DURATION}" \
          PERF_CONFIGURATION="${CONFIGURATION}" \
          dotnet run -c "${CONFIGURATION}" --no-restore --project "${PROJECT}" -- \
          "${pattern}" "${transport}" "${size}" > "${tmp_log}" 2>&1; then
          extracted="$(extract_required_results "${tmp_log}" "${pattern}" "${transport}" "${size}")"
          while IFS= read -r result_line; do
            [[ -n "${result_line}" ]] || continue
            print_line "${result_line}"
            printf '%s\n' "${result_line}" >> "${metrics_file}"
            result_lines=$((result_lines + 1))
          done <<< "${extracted}"
        else
          cat "${tmp_log}" >&2 || true
          echo "FAIL pattern=${pattern} transport=${transport} size=${size} run=${run_index}" >&2
          status=1
        fi
      done

      if [[ -s "${metrics_file}" ]]; then
        print_line ""
        emit_markdown_table "${metrics_file}" "${pattern}" "${transport}" "${run_index}"
        print_line ""
      fi
    done
  done
done

print_line "## Effective Options (result)"
print_line "- lang: dotnet"
print_line "- suite: single"
print_line "- runs: ${RUNS}"
print_line "- duration_seconds: ${DURATION}"
print_line "- warmup_seconds: ${WARMUP}"
print_line "- patterns: ${PATTERN}"
print_line "- transports: ${TRANSPORTS}"
print_line "- msg_sizes: ${MSG_SIZES}"
print_line "- pin_cpu: off"
print_line "- expected_result_lines: ${expected_result_lines}"
print_line "- actual_result_lines: ${result_lines}"
print_line "- status: $( [[ "${status}" -eq 0 ]] && printf 'complete' || printf 'failed' )"

echo "saved report: ${REPORT}"
exit "${status}"
