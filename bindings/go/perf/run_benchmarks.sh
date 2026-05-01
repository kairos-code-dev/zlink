#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${ROOT_DIR}"

export GOCACHE="${GOCACHE:-/tmp/zlink-go-cache}"
export GOTMPDIR="${GOTMPDIR:-/tmp/zlink-go-tmp}"
mkdir -p "${GOCACHE}" "${GOTMPDIR}"

PATTERN="ALL"
DURATION="5"
MSG_SIZES="${PERF_MSG_SIZES:-64,256,1024,65536,131072,262144}"
TRANSPORTS="${PERF_TRANSPORTS:-}"
RUNS="1"
RESULTS_DIR="${SCRIPT_DIR}/results/single/report"
RESULTS_TAG=""
OUTPUT_FILE=""
PIN_CPU="off"
IO_THREADS=""
HWM=""
SEND_HWM=""
RECV_HWM=""
SNDTIMEO_MS=""
RCVTIMEO_MS=""

cleanup_report_dir() {
  local dir="$1"
  local max_files="100"
  mkdir -p "${dir}"
  mapfile -t existing < <(find "${dir}" -maxdepth 1 -type f -name 'perf_go_single_*.txt' | sort)
  while [[ "${#existing[@]}" -ge "${max_files}" ]]; do
    rm -f "${existing[0]}"
    existing=("${existing[@]:1}")
  done
}

resolve_results_dir() {
  local dir="$1"
  case "${dir}" in
    */single/report|*/report)
      echo "${dir}"
      ;;
    *)
      echo "${dir}/single/report"
      ;;
  esac
}

usage() {
  cat <<'USAGE'
Usage: bindings/go/perf/run_benchmarks.sh [options]

Options:
  --pattern NAME
  --duration N
  --msg-sizes LIST
  --transports LIST
  --runs N
  --results-dir PATH
  --results-tag NAME
  --output PATH
  --build-dir PATH
  --reuse-build
  --clean-build
  --pin-cpu
  --io-threads N
  --hwm N
  --send-hwm N
  --recv-hwm N
  --sndtimeo N
  --rcvtimeo N
  --send-timeout-ms N
  --recv-timeout-ms N
  -h, --help

Notes:
  - Supported single patterns: PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER,SPOT,SPOT_REQREP
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help) usage; exit 0 ;;
    --pattern) PATTERN="$2"; shift 2 ;;
    --duration) DURATION="$2"; shift 2 ;;
    --msg-sizes) MSG_SIZES="$2"; shift 2 ;;
    --transports) TRANSPORTS="$2"; shift 2 ;;
    --runs) RUNS="$2"; shift 2 ;;
    --results-dir) RESULTS_DIR="$2"; shift 2 ;;
    --results-tag) RESULTS_TAG="$2"; shift 2 ;;
    --output) OUTPUT_FILE="$2"; shift 2 ;;
    --build-dir)
      shift 2 ;;
    --io-threads)
      IO_THREADS="$2"
      shift 2 ;;
    --hwm)
      HWM="$2"
      shift 2 ;;
    --send-hwm)
      SEND_HWM="$2"
      shift 2 ;;
    --recv-hwm)
      RECV_HWM="$2"
      shift 2 ;;
    --sndtimeo|--send-timeout-ms)
      SNDTIMEO_MS="$2"
      shift 2 ;;
    --rcvtimeo|--recv-timeout-ms)
      RCVTIMEO_MS="$2"
      shift 2 ;;
    --reuse-build|--clean-build)
      shift ;;
    --pin-cpu)
      PIN_CPU="on"
      shift ;;
    *)
      echo "Error: unknown option $1" >&2
      exit 1 ;;
  esac
done

if [[ -n "${HWM}" ]]; then
  export PERF_SINGLE_HWM="${HWM}"
fi
if [[ -n "${IO_THREADS}" ]]; then
  export PERF_IO_THREADS="${IO_THREADS}"
fi
if [[ -n "${SEND_HWM}" ]]; then
  export PERF_SINGLE_SNDHWM="${SEND_HWM}"
fi
if [[ -n "${RECV_HWM}" ]]; then
  export PERF_SINGLE_RCVHWM="${RECV_HWM}"
fi
if [[ -n "${SNDTIMEO_MS}" ]]; then
  export PERF_SINGLE_SNDTIMEO_MS="${SNDTIMEO_MS}"
fi
if [[ -n "${RCVTIMEO_MS}" ]]; then
  export PERF_SINGLE_RCVTIMEO_MS="${RCVTIMEO_MS}"
fi

case "$(uname -s)" in
  Linux*) PLATFORM="linux" ;;
  Darwin*) PLATFORM="macos" ;;
  *) PLATFORM="windows" ;;
esac

run_go_perf() {
  if [[ "${PIN_CPU}" != "on" ]]; then
    go run "$@"
    return
  fi

  case "$(uname -s)" in
    Linux*)
      if ! command -v taskset >/dev/null 2>&1; then
        echo "Error: --pin-cpu requires taskset on Linux" >&2
        return 1
      fi
      taskset -c 0 go run "$@"
      ;;
    *)
      echo "Error: --pin-cpu is not supported by this runner on $(uname -s)" >&2
      return 1
      ;;
  esac
}

TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
TAG_SUFFIX=""
if [[ -n "${RESULTS_TAG}" ]]; then
  TAG_SUFFIX="_${RESULTS_TAG}"
fi
RESULTS_DIR="$(resolve_results_dir "${RESULTS_DIR}")"
RESULTS_FILE="${RESULTS_DIR}/perf_go_single_${PLATFORM}_${TIMESTAMP}${TAG_SUFFIX}.txt"
mkdir -p "${RESULTS_DIR}"
cleanup_report_dir "${RESULTS_DIR}"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/zlink-go-single.XXXXXX")"
cleanup() {
  rm -rf "${TMP_DIR}"
}
trap cleanup EXIT

IFS=',' read -r -a SIZES <<< "${MSG_SIZES}"
if [[ "${PATTERN}" == "ALL" ]]; then
  PATTERNS=("PAIR" "PUBSUB" "DEALER_DEALER" "DEALER_ROUTER" "ROUTER_ROUTER" "SPOT" "SPOT_REQREP")
else
  IFS=',' read -r -a PATTERNS <<< "${PATTERN}"
fi

if [[ -n "${TRANSPORTS}" ]]; then
  IFS=',' read -r -a XPORTS_FILTER <<< "${TRANSPORTS}"
else
  XPORTS_FILTER=()
fi

pattern_transports() {
  case "$1" in
    SPOT|SPOT_REQREP) echo "tcp tls ws wss" ;;
    *)
      if [[ "${PLATFORM}" == "windows" ]]; then
        echo "tcp tls ws wss inproc"
      else
        echo "tcp tls ws wss inproc ipc"
      fi
      ;;
  esac
}

transport_enabled() {
  local transport="$1"
  if [[ "${#XPORTS_FILTER[@]}" -eq 0 ]]; then
    return 0
  fi
  local candidate
  for candidate in "${XPORTS_FILTER[@]}"; do
    if [[ "${candidate}" == "${transport}" ]]; then
      return 0
    fi
  done
  return 1
}

join_by() {
  local delim="$1"
  shift
  local first=1
  local item
  for item in "$@"; do
    if [[ "${first}" -eq 1 ]]; then
      printf '%s' "${item}"
      first=0
    else
      printf '%s%s' "${delim}" "${item}"
    fi
  done
}

resolve_single_effective_transports() {
  declare -A seen=()
  local pattern transport
  local resolved=()
  for pattern in "${PATTERNS[@]}"; do
    read -r -a PATTERN_XPORTS <<< "$(pattern_transports "${pattern}")"
    for transport in "${PATTERN_XPORTS[@]}"; do
      if ! transport_enabled "${transport}"; then
        continue
      fi
      if [[ -n "${seen[${transport}]:-}" ]]; then
        continue
      fi
      seen["${transport}"]=1
      resolved+=("${transport}")
    done
  done
  join_by "," "${resolved[@]}"
}

EFFECTIVE_PATTERNS_CSV="$(join_by "," "${PATTERNS[@]}")"
EFFECTIVE_TRANSPORTS_CSV="$(resolve_single_effective_transports)"

append_case_output() {
  local case_log="$1"
  cat "${case_log}" >> "${RESULTS_FILE}"
  if [[ -s "${case_log}" ]]; then
    printf '\n' >> "${RESULTS_FILE}"
  fi
}

progress_header_printed=0

progress_pattern_heading() {
  local pattern="$1"
  if [[ "${progress_pattern_heading_last:-}" == "${pattern}" ]]; then
    return
  fi
  progress_pattern_heading_last="${pattern}"
  echo "  > Benchmarking current for ${pattern}..."
}

progress_table_header() {
  if [[ "${progress_header_printed}" -eq 1 ]]; then
    return
  fi
  progress_header_printed=1
  echo "      | Size     |       Throughput |  Bandwidth |  Lat.Mean(ms) |   Lat.P95(ms) |   Lat.P99(ms) |"
  echo "      |----------|------------------|------------|---------------|---------------|---------------|"
}

progress_case_row() {
  local pattern="$1"
  local size="$2"
  local case_log="$3"
  if grep -Eq '^UNSUPPORTED,' "${case_log}"; then
    printf '      | %sB | %16s | %10s | %13s | %13s | %13s |\n' "${size}" "UNSUPPORTED" "UNSUPPORTED" "UNSUPPORTED" "UNSUPPORTED" "UNSUPPORTED"
    return
  fi
  if grep -Eq '^SKIP,' "${case_log}"; then
    printf '      | %sB | %16s | %10s | %13s | %13s | %13s |\n' "${size}" "SKIP" "SKIP" "SKIP" "SKIP" "SKIP"
    return
  fi
  python3 - "$pattern" "$size" "$case_log" <<'PY'
import sys

pattern, size, path = sys.argv[1], sys.argv[2], sys.argv[3]
metrics = {}
with open(path, "r", encoding="utf-8", errors="replace") as fh:
    for raw in fh:
        parts = raw.strip().split(",")
        if len(parts) != 7 or parts[0] != "RESULT" or parts[1] != "current":
            continue
        if parts[2] != pattern or parts[4] != size:
            continue
        metrics[parts[5]] = parts[6]

required = ("throughput", "bandwidth", "latency", "latency_p95", "latency_p99")
if not all(key in metrics for key in required):
    print(f"      | {size}B | {'FAIL':>16} | {'FAIL':>10} | {'FAIL':>13} | {'FAIL':>13} | {'FAIL':>13} |")
    raise SystemExit(0)

unit = "Kops/s" if pattern == "SPOT_REQREP" else "Kmsg/s"
throughput = float(metrics["throughput"]) / 1000.0
bandwidth = float(metrics["bandwidth"])
latency = float(metrics["latency"])
latency_p95 = float(metrics["latency_p95"])
latency_p99 = float(metrics["latency_p99"])
print(
    f"      | {size}B | {throughput:16.2f} {unit} | {bandwidth:10.2f} MB/s |"
    f" {latency:13.3f} ms | {latency_p95:13.3f} ms | {latency_p99:13.3f} ms |"
)
PY
}

count_result_lines() {
  local pattern="$1"
  local transport="$2"
  local size="$3"
  local case_log="$4"
  awk -F',' -v pattern="${pattern}" -v transport="${transport}" -v size="${size}" '
    $1 == "RESULT" && $2 == "current" && $3 == pattern && $4 == transport && $5 == size { count++ }
    END { print count + 0 }
  ' "${case_log}"
}

render_tables() {
  python3 - "$TMP_DIR" <<'PY'
from collections import defaultdict
import os
import re
import statistics
import sys

tmp_dir = sys.argv[1]
metrics = ("throughput", "bandwidth", "latency", "latency_p95", "latency_p99")
echo_patterns = {"SPOT_REQREP"}
rows = defaultdict(lambda: defaultdict(list))
for entry in os.listdir(tmp_dir):
    if not entry.endswith(".log"):
        continue
    path = os.path.join(tmp_dir, entry)
    with open(path, "r", encoding="utf-8") as fh:
        for raw in fh:
            parts = raw.strip().split(",")
            if len(parts) != 7 or parts[0] != "RESULT" or parts[1] != "current":
                continue
            pattern, transport, size, metric, value = parts[2], parts[3], parts[4], parts[5], parts[6]
            if metric not in metrics:
                continue
            try:
                rows[(pattern, transport, int(size))][metric].append(float(value))
            except ValueError:
                continue

by_pattern = defaultdict(list)
for key, metric_values in rows.items():
    values = {}
    for metric in metrics:
        samples = metric_values.get(metric)
        if not samples:
            break
        values[metric] = statistics.median(samples)
    if len(values) != len(metrics):
        continue
    pattern, transport, size = key
    by_pattern[pattern].append((transport, size, values))

printed_first_pattern = False
for pattern in sorted(by_pattern):
    if printed_first_pattern:
        print()
        print("===============================================================================")
        print()
    printed_first_pattern = True
    direction = "echo" if pattern in echo_patterns else "one-way"
    print(f"## PATTERN: {pattern} ({direction})")
    print()
    transports = defaultdict(list)
    for transport, size, values in by_pattern[pattern]:
        transports[transport].append((size, values))
    for transport in sorted(transports):
        print(f"### Transport: {transport}")
        print("| Size     |       Throughput |  Bandwidth |  Lat.Mean(ms) |   Lat.P95(ms) |   Lat.P99(ms) |")
        print("|----------|------------------|------------|---------------|---------------|---------------|")
        for size, values in sorted(transports[transport]):
            if not all(metric in values for metric in metrics):
                continue
            unit = "Kops/s" if pattern in echo_patterns else "Kmsg/s"
            print(
                f"| {size}B"
                f" | {values['throughput'] / 1000.0:16.2f} {unit}"
                f" | {values['bandwidth']:10.2f} MB/s"
                f" | {values['latency']:13.3f} ms"
                f" | {values['latency_p95']:13.3f} ms"
                f" | {values['latency_p99']:13.3f} ms |"
            )
        print()
PY
}

{
  echo "## Effective Options (start)"
  echo "- lang: go"
  echo "- suite: single"
  echo "- runs: ${RUNS}"
  echo "- patterns: ${EFFECTIVE_PATTERNS_CSV}"
  echo "- transports: ${EFFECTIVE_TRANSPORTS_CSV}"
  echo "- msg_sizes: ${MSG_SIZES}"
  echo "- pin_cpu: ${PIN_CPU}"
  echo
} > "${RESULTS_FILE}"

result_lines=0
unsupported_cases=0
skip_cases=0
fail=0
expected_cases=0
FAILURES=()

for run in $(seq 1 "${RUNS}"); do
  for pattern in "${PATTERNS[@]}"; do
    progress_pattern_heading "${pattern}"
    read -r -a PATTERN_XPORTS <<< "$(pattern_transports "${pattern}")"
    for transport in "${PATTERN_XPORTS[@]}"; do
      if ! transport_enabled "${transport}"; then
        continue
      fi
      echo "    Testing ${transport}:"
      if [[ "${RUNS}" -gt 1 ]]; then
        echo "      run ${run}/${RUNS}:"
      fi
      progress_header_printed=0
      for size in "${SIZES[@]}"; do
        expected_cases=$((expected_cases + 1))
        case_log="${TMP_DIR}/${pattern}_${transport}_${size}_run${run}.log"
        if run_go_perf ./perf/single \
          --pattern "${pattern}" \
          --transport "${transport}" \
          --msg-size "${size}" \
          --duration "${DURATION}" \
          > "${case_log}" 2>&1; then
          append_case_output "${case_log}"
          case_result_lines="$(count_result_lines "${pattern}" "${transport}" "${size}" "${case_log}")"
          if grep -Eq '^UNSUPPORTED,' "${case_log}"; then
            unsupported_cases=$((unsupported_cases + 1))
            expected_cases=$((expected_cases - 1))
            progress_table_header
            progress_case_row "${pattern}" "${size}" "${case_log}"
            continue
          fi
          if grep -Eq '^SKIP,' "${case_log}"; then
            skip_cases=$((skip_cases + 1))
            expected_cases=$((expected_cases - 1))
            progress_table_header
            progress_case_row "${pattern}" "${size}" "${case_log}"
            continue
          fi
          if [[ "${case_result_lines}" -eq 0 ]]; then
            echo "FAIL,current,${pattern},${transport},${size},no_result_lines" >> "${RESULTS_FILE}"
            fail=$((fail + 1))
            FAILURES+=("${pattern} current ${transport} ${size}B: no_result_lines")
          else
            result_lines=$((result_lines + case_result_lines))
          fi
        else
          append_case_output "${case_log}"
          if grep -Eq '^UNSUPPORTED,' "${case_log}"; then
            unsupported_cases=$((unsupported_cases + 1))
            expected_cases=$((expected_cases - 1))
            progress_table_header
            progress_case_row "${pattern}" "${size}" "${case_log}"
            continue
          fi
          echo "FAIL,current,${pattern},${transport},${size},exit_nonzero" >> "${RESULTS_FILE}"
          fail=$((fail + 1))
          FAILURES+=("${pattern} current ${transport} ${size}B: exit_nonzero")
        fi
        progress_table_header
        progress_case_row "${pattern}" "${size}" "${case_log}"
      done
      echo "    Testing ${transport}: Done"
    done
  done
done

table_output="$(render_tables)"
if [[ -n "${table_output}" ]]; then
  printf '\n%s\n' "${table_output}" >> "${RESULTS_FILE}"
fi

if [[ "${#FAILURES[@]}" -gt 0 ]]; then
  {
    echo
    echo "## Failures"
    for failure in "${FAILURES[@]}"; do
      echo "- ${failure}"
    done
  } >> "${RESULTS_FILE}"
fi

expected_result_lines=$((expected_cases * 5))
status="partial"
if [[ "${fail}" -eq 0 && "${result_lines}" -eq "${expected_result_lines}" ]]; then
  status="complete"
fi

{
  expected_result_lines=$((expected_cases * 5))
  echo
  echo "## Effective Options (result)"
  echo "- lang: go"
  echo "- suite: single"
  echo "- runs: ${RUNS}"
  echo "- patterns: ${EFFECTIVE_PATTERNS_CSV}"
  echo "- transports: ${EFFECTIVE_TRANSPORTS_CSV}"
  echo "- msg_sizes: ${MSG_SIZES}"
  echo "- pin_cpu: ${PIN_CPU}"
  echo "- status: ${status}"
  echo "- expected_result_lines: ${expected_result_lines}"
  echo "- actual_result_lines: ${result_lines}"
} >> "${RESULTS_FILE}"

if [[ -n "${OUTPUT_FILE}" ]]; then
  cp "${RESULTS_FILE}" "${OUTPUT_FILE}"
fi

cat "${RESULTS_FILE}"

if [[ "${status}" != "complete" ]]; then
  exit 1
fi
