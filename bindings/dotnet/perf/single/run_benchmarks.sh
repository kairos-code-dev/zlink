#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOTNET_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PROJECT="${DOTNET_DIR}/perf/single/Zlink.BindingBench/Zlink.BindingBench.csproj"
PROJECT_DIR="${DOTNET_DIR}/perf/single/Zlink.BindingBench"
RESULTS_ROOT="${DOTNET_DIR}/perf/results"
PATTERN="ALL"
TRANSPORTS="${PERF_TRANSPORTS:-}"
MSG_SIZES="${PERF_MSG_SIZES:-64,256,1024,65536,131072,262144}"
DURATION="${PERF_SINGLE_DURATION_SECONDS:-5}"
RUNS="${PERF_RUNS:-1}"
RESULTS_TAG=""
CONFIGURATION="${PERF_CONFIGURATION:-Release}"
REPORT=""
TMP_DIR=""

prune_report_dir() {
  local report_dir="$1"
  python3 - "${report_dir}" <<'PY'
import pathlib
import sys

report_dir = pathlib.Path(sys.argv[1])
if not report_dir.exists():
    raise SystemExit(0)

files = sorted(
    [p for p in report_dir.iterdir() if p.is_file()],
    key=lambda p: p.name,
)
overflow = len(files) - 100
for path in files[:max(0, overflow)]:
    try:
        path.unlink()
    except FileNotFoundError:
        pass
PY
}

resolve_perf_binary() {
  local project_dir="$1"
  local assembly_name="$2"
  local binary_path="${project_dir}/bin/${CONFIGURATION}/net8.0/${assembly_name}"
  local dll_path="${project_dir}/bin/${CONFIGURATION}/net8.0/${assembly_name}.dll"
  if [[ -x "${binary_path}" ]]; then
    printf '%s' "${binary_path}"
    return 0
  fi
  if [[ -f "${dll_path}" ]]; then
    printf 'dotnet %q' "${dll_path}"
    return 0
  fi
  return 1
}

usage() {
  cat <<'USAGE'
Usage: perf/single/run_benchmarks.sh [options]

Measure current zlink .NET binding single-pattern performance.

Options:
  -h, --help            Show this help.
  --pattern NAME        Pattern list (comma-separated) or ALL.
  --duration N          Active duration seconds (default: 5).
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

pattern_supports_transport() {
  local pattern="${1:-}"
  local transport="${2:-}"
  case "${pattern}" in
    SPOT)
      [[ "${transport}" =~ ^(tcp|tls|ws|wss)$ ]]
      ;;
    *)
      if [[ "$(uname -s)" == "Windows_NT" || "$(uname -s)" =~ ^(MINGW|MSYS|CYGWIN) ]]; then
        [[ "${transport}" =~ ^(tcp|tls|ws|wss|inproc)$ ]]
      else
        [[ "${transport}" =~ ^(tcp|tls|ws|wss|inproc|ipc)$ ]]
      fi
      ;;
  esac
}

default_transports_for_pattern() {
  local pattern="${1:-}"
  if [[ "${pattern}" == "SPOT" ]]; then
    printf '%s' "tcp,tls,ws,wss"
    return
  fi

  if [[ "$(uname -s)" == "Linux" ]]; then
    printf '%s' "tcp,tls,ws,wss,inproc,ipc"
  else
    printf '%s' "tcp,tls,ws,wss,inproc"
  fi
}

extract_unsupported_line() {
  local log_path="${1:-}"
  local pattern="${2:-}"
  local transport="${3:-}"

  python3 - "${log_path}" "${pattern}" "${transport}" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
pattern = sys.argv[2]
transport = sys.argv[3]
needle = f"UNSUPPORTED,current,{pattern},{transport}"

if not path.exists():
    raise SystemExit(1)

text = path.read_text(encoding="utf-8", errors="replace")
for line in text.splitlines():
    if line.strip() == needle:
        print(needle)
        raise SystemExit(0)

if transport in {"tcp", "tls", "ws", "wss", "ipc"}:
    lowered = text.lower()
    if ("permission denied" in lowered
            or "operation not permitted" in lowered
            or "zlinkbindexception" in lowered
            or "zlinkconnectexception" in lowered
            or "legacyzlinkexception" in lowered
            or "socketexception" in lowered
            or "errno 1" in lowered
            or "errno 13" in lowered):
        print(needle)
        raise SystemExit(0)

raise SystemExit(1)
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

print("      | Size     |       Throughput |   Bandwidth | Lat.Mean(ms) | Lat.P95(ms) | Lat.P99(ms) |")
print("      |----------|------------------|-------------|--------------|-------------|-------------|")
for size, metrics in rows.items():
    values = [metrics.get(metric, "NA") for metric in required]
    throughput = float(values[0]) / 1000.0
    bandwidth = float(values[1])
    latency = float(values[2])
    latency_p95 = float(values[3])
    latency_p99 = float(values[4])
    print(
        f"      | {size}B | {throughput:>16.2f} Kmsg/s | {bandwidth:>10.2f} MB/s |"
        f" {latency:>11.3f} ms | {latency_p95:>11.3f} ms | {latency_p99:>11.3f} ms |"
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

mkdir -p "${RESULTS_ROOT}/single/report"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/zlink-dotnet-single.XXXXXX")"
cleanup_tmp_dir() {
  if [[ -n "${TMP_DIR}" && -d "${TMP_DIR}" ]]; then
    rm -rf "${TMP_DIR}"
  fi
}
trap cleanup_tmp_dir EXIT

platform="$(normalize_platform)"
timestamp="$(date +%Y%m%d_%H%M%S)"
report_base="perf_dotnet_single_${platform}_${timestamp}"
if [[ -n "${RESULTS_TAG}" ]]; then
  report_base="${report_base}_${RESULTS_TAG}"
fi
REPORT="${RESULTS_ROOT}/single/report/${report_base}.txt"
: > "${REPORT}"
prune_report_dir "${RESULTS_ROOT}/single/report"

if ! PERF_BINARY="$(resolve_perf_binary "${PROJECT_DIR}" "Zlink.BindingBench")"; then
  echo "single benchmark binary not found under ${PROJECT_DIR}/bin/${CONFIGURATION}/net8.0." >&2
  echo "Gate 1 build output is required before smoke." >&2
  exit 1
fi

print_line "## Effective Options (start)"
print_line "- lang: dotnet"
print_line "- suite: single"
print_line "- runs: ${RUNS}"
print_line "- duration_seconds: ${DURATION}"
print_line "- patterns: ${PATTERN}"
print_line "- transports: ${TRANSPORTS:-auto(pattern)}"
print_line "- msg_sizes: ${MSG_SIZES}"
print_line "- pin_cpu: off"
print_line ""

IFS=',' read -r -a patterns <<< "${PATTERN}"
if [[ -n "${TRANSPORTS}" ]]; then
  IFS=',' read -r -a transports_filter <<< "${TRANSPORTS}"
else
  transports_filter=()
fi
IFS=',' read -r -a msg_sizes <<< "${MSG_SIZES}"

status=0
result_lines=0
expected_result_lines=0
for (( run_index=1; run_index<=RUNS; run_index++ )); do
  for pattern in "${patterns[@]}"; do
    pattern="${pattern//[[:space:]]/}"
    [[ -n "${pattern}" ]] || continue

    if [[ "${#transports_filter[@]}" -gt 0 ]]; then
      transports=("${transports_filter[@]}")
    else
      IFS=',' read -r -a transports <<< "$(default_transports_for_pattern "${pattern}")"
    fi

    for transport in "${transports[@]}"; do
      transport="${transport//[[:space:]]/}"
      [[ -n "${transport}" ]] || continue

      if ! pattern_supports_transport "${pattern}" "${transport}"; then
        print_line "UNSUPPORTED,current,${pattern},${transport}"
        continue
      fi

      metrics_file="${TMP_DIR}/${pattern,,}_${transport}_run${run_index}.metrics"
      : > "${metrics_file}"

      for size in "${msg_sizes[@]}"; do
        size="${size//[[:space:]]/}"
        [[ -n "${size}" ]] || continue
        expected_result_lines=$((expected_result_lines + 5))

        tmp_log="${TMP_DIR}/${pattern,,}_${transport}_${size}_run${run_index}.log"
        echo "RUN pattern=${pattern} transport=${transport} size=${size} run=${run_index}"
        if DOTNET_TieredCompilation=0 \
          PERF_SINGLE_DURATION_SECONDS="${DURATION}" \
          PERF_CONFIGURATION="${CONFIGURATION}" \
          bash -lc "${PERF_BINARY@Q} ${pattern@Q} ${transport@Q} ${size@Q}" \
          > "${tmp_log}" 2>&1; then
          if unsupported_line="$(extract_unsupported_line "${tmp_log}" "${pattern}" "${transport}" 2>/dev/null)"; then
            print_line "${unsupported_line}"
            expected_result_lines=$((expected_result_lines - 5))
            continue
          fi
          extracted="$(extract_required_results "${tmp_log}" "${pattern}" "${transport}" "${size}")"
          while IFS= read -r result_line; do
            [[ -n "${result_line}" ]] || continue
            print_line "${result_line}"
            printf '%s\n' "${result_line}" >> "${metrics_file}"
            result_lines=$((result_lines + 1))
          done <<< "${extracted}"
        else
          if unsupported_line="$(extract_unsupported_line "${tmp_log}" "${pattern}" "${transport}" 2>/dev/null)"; then
            print_line "${unsupported_line}"
            expected_result_lines=$((expected_result_lines - 5))
            continue
          fi
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
print_line "- patterns: ${PATTERN}"
print_line "- transports: ${TRANSPORTS:-auto(pattern)}"
print_line "- msg_sizes: ${MSG_SIZES}"
print_line "- pin_cpu: off"
print_line "- expected_result_lines: ${expected_result_lines}"
print_line "- actual_result_lines: ${result_lines}"
if [[ "${result_lines}" -eq "${expected_result_lines}" && "${status}" -eq 0 ]]; then
  completion_status="complete"
else
  completion_status="partial"
  status=1
fi
print_line "- status: ${completion_status}"

echo "saved report: ${REPORT}"
exit "${status}"
