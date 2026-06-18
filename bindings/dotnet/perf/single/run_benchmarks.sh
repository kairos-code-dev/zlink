#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOTNET_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
REPO_DIR="$(cd "${DOTNET_DIR}/../.." && pwd)"
source "${DOTNET_DIR}/perf/common/report_helpers.sh"
PROJECT="${DOTNET_DIR}/perf/single/Zlink.BindingBench/Zlink.BindingBench.csproj"
PROJECT_DIR="${DOTNET_DIR}/perf/single/Zlink.BindingBench"
VERSION_FILE="${REPO_DIR}/VERSION"
CORE_LIB_DIR="${REPO_DIR}/core/build/lib"
CORE_VERSION="$(awk -F= '/^LIBZLINK_VERSION=/{print $2}' "${VERSION_FILE}")"
CORE_LIB="${CORE_LIB_DIR}/libzlink.so.${CORE_VERSION}"
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
REUSE_BUILD=0
CLEAN_BUILD=0
BUILD_DIR=""
SECONDS=0
SHOW_TOTAL_TIME=0

format_elapsed() {
  local total_sec="${1:-0}"
  local hours=$(( total_sec / 3600 ))
  local minutes=$(( (total_sec % 3600) / 60 ))
  local seconds=$(( total_sec % 60 ))
  if (( hours > 0 )); then
    printf "%dh %dm %ds" "${hours}" "${minutes}" "${seconds}"
  elif (( minutes > 0 )); then
    printf "%dm %ds" "${minutes}" "${seconds}"
  else
    printf "%ds" "${seconds}"
  fi
}

print_total_time() {
  if [[ "${SHOW_TOTAL_TIME}" -ne 1 ]]; then
    return
  fi
  if [[ "${PERF_SUPPRESS_TOTAL_TIME:-0}" == "1" ]]; then
    return
  fi
  local status="${1:-0}"
  local elapsed="${SECONDS}"
  echo "Total benchmark time: $(format_elapsed "${elapsed}") (${elapsed}s, exit=${status})"
}
trap 'print_total_time $?' EXIT

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
  --build-dir PATH      Accepted for policy compatibility.
  --reuse-build         Reuse existing build output.
  --clean-build         Remove project bin/obj before build.
  --output PATH         Tee report output to PATH.
  --pin-cpu             Pin benchmark process to CPU 0 on Linux.
  --io-threads N        Context I/O threads.
  --hwm N               Debug-only manual HWM override.
  --send-hwm N          Send HWM override.
  --recv-hwm N          Receive HWM override.
  --buf SIZE            Send/receive buffer override.
  --sndbuf SIZE         Send buffer override.
  --rcvbuf SIZE         Receive buffer override.
  --sndtimeo N          Send timeout ms.
  --rcvtimeo N          Receive timeout ms.
  --send-timeout-ms N   Alias of --sndtimeo.
  --recv-timeout-ms N   Alias of --rcvtimeo.
  --auto-hwm-profile NAME Auto-HWM profile.
  --results-dir PATH    Override result root directory.
  --results-tag NAME    Optional report suffix tag.

Notes:
  - result is saved under results/single/report/ as
    perf_dotnet_single_<platform>_YYYYMMDD_HHMMSS[_<tag>].txt.
USAGE
}

ensure_build_output() {
  if [[ "${CLEAN_BUILD}" -eq 1 ]]; then
    rm -rf "${PROJECT_DIR}/bin" "${PROJECT_DIR}/obj"
  fi
  if [[ "${REUSE_BUILD}" -eq 1 ]]; then
    return
  fi

  dotnet build "${PROJECT}" -c "${CONFIGURATION}" >/dev/null
}

sync_native_dirs() {
  local search_root="$1"
  [[ -d "${search_root}" ]] || return 0

  while IFS= read -r native_dir; do
    rm -f "${native_dir}/libzlink.so" \
      "${native_dir}/libzlink.so.7" \
      "${native_dir}/libzlink.so."*
    cp -f "${CORE_LIB}" "${native_dir}/libzlink.so.${CORE_VERSION}"
    ln -sfn "libzlink.so.${CORE_VERSION}" "${native_dir}/libzlink.so.7"
    ln -sfn libzlink.so.7 "${native_dir}/libzlink.so"
  done < <(find "${search_root}" -type d -path '*linux-x64/native')
}

prepare_core_runtime() {
  if [[ ! -f "${CORE_LIB}" ]]; then
    echo "core runtime not found: ${CORE_LIB}" >&2
    echo "Build core/build before running dotnet perf." >&2
    exit 1
  fi
  if find "${REPO_DIR}/core/include" "${REPO_DIR}/core/src" \
      -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.c' -o -name '*.cc' -o -name '*.cpp' \) \
      -newer "${CORE_LIB}" -print -quit | grep -q .; then
    echo "core runtime is older than core source: ${CORE_LIB}" >&2
    echo "Run: cmake --build core/build" >&2
    exit 1
  fi
  echo "Perf runtime libzlink: ${CORE_LIB}"
  export ZLINK_LIBRARY_PATH="${CORE_LIB}"
  sync_native_dirs "${PROJECT_DIR}/bin"
}

normalize_platform() {
  case "$(uname -s)" in
    Linux*) printf 'linux' ;;
    Darwin*) printf 'macos' ;;
    MINGW*|MSYS*|CYGWIN*) printf 'windows' ;;
    *) uname -s | tr '[:upper:]' '[:lower:]' ;;
  esac
}

EMITTER="${SCRIPT_DIR}/run_emit.py"

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
needle = f"UNSUPPORTED,dotnet,{pattern},{transport}"

if not path.exists():
    raise SystemExit(1)

text = path.read_text(encoding="utf-8", errors="replace")
for line in text.splitlines():
    if line.strip() == needle:
        print(needle)
        raise SystemExit(0)

raise SystemExit(1)
PY
}

emit_result_row() {
  local metrics_file="${1:-}"

  if [[ ! -s "${metrics_file}" ]]; then
    return
  fi

  python3 - "${metrics_file}" <<'PY'
import csv
import sys

required = [
    "throughput",
    "bandwidth",
    "latency",
    "latency_p95",
    "latency_p99",
]
metrics = {}
size = ""
with open(sys.argv[1], encoding="utf-8") as handle:
    reader = csv.reader(handle)
    for row in reader:
        if len(row) != 7 or row[0] != "RESULT":
            continue
        size = row[4]
        metrics[row[5]] = row[6]

values = [metrics.get(metric, "0") for metric in required]
throughput = float(values[0]) / 1000.0
bandwidth = float(values[1])
latency = float(values[2])
latency_p95 = float(values[3])
latency_p99 = float(values[4])
throughput_text = f"{throughput:8.3f} Kmsg/s"
print(
    f"      | {size + 'B':<8} | {throughput_text:>16} | {bandwidth:>10.3f} MB/s |"
    f" {latency:>9.3f} ms | {latency_p95:>9.3f} ms | {latency_p99:>9.3f} ms |"
)
PY
}

emit_failure_row() {
  local size="${1:-}"
  print_line "      | ${size}B      | FAIL               | FAIL           | FAIL          | FAIL          | FAIL          |"
}

# Emit one indented (run/median) table data row. Mirrors emit_result_row but
# accepts an explicit left indent so per-run / median tables match C
# run_comparison.py single_table_row_line indentation (8 spaces under the
# "run N/runs:" / "median:" labels) for runs>1.
emit_result_row_indent() {
  local metrics_file="${1:-}"
  local indent="${2:-      }"

  if [[ ! -s "${metrics_file}" ]]; then
    return
  fi

  python3 - "${metrics_file}" "${indent}" <<'PY'
import csv
import sys

required = ["throughput", "bandwidth", "latency", "latency_p95", "latency_p99"]
metrics = {}
size = ""
with open(sys.argv[1], encoding="utf-8") as handle:
    reader = csv.reader(handle)
    for row in reader:
        if len(row) != 7 or row[0] != "RESULT":
            continue
        size = row[4]
        metrics[row[5]] = row[6]

indent = sys.argv[2]
values = [metrics.get(metric, "0") for metric in required]
throughput = float(values[0]) / 1000.0
bandwidth = float(values[1])
latency = float(values[2])
latency_p95 = float(values[3])
latency_p99 = float(values[4])
throughput_text = f"{throughput:8.3f} Kmsg/s"
print(
    f"{indent}| {size + 'B':<8} | {throughput_text:>16} | {bandwidth:>10.3f} MB/s |"
    f" {latency:>9.3f} ms | {latency_p95:>9.3f} ms | {latency_p99:>9.3f} ms |"
)
PY
}

# Per-metric median across the per-run RESULT lines collected for one
# (pattern,transport,size). Matches C run_comparison.py: statistics.median()
# applied independently to throughput/bandwidth/latency/p95/p99, then printed
# as one indented "median:" data row.
emit_median_row_indent() {
  local samples_file="${1:-}"
  local size="${2:-}"
  local indent="${3:-        }"

  if [[ ! -s "${samples_file}" ]]; then
    return 1
  fi

  python3 - "${samples_file}" "${size}" "${indent}" <<'PY'
import csv
import statistics
import sys

required = ["throughput", "bandwidth", "latency", "latency_p95", "latency_p99"]
samples = {metric: [] for metric in required}
with open(sys.argv[1], encoding="utf-8", errors="replace") as handle:
    reader = csv.reader(handle)
    for row in reader:
        if len(row) != 7 or row[0] != "RESULT":
            continue
        if row[5] in required:
            try:
                samples[row[5]].append(float(row[6]))
            except ValueError:
                pass

if any(not samples[metric] for metric in required):
    raise SystemExit(1)

size = sys.argv[2]
indent = sys.argv[3]
med = {metric: statistics.median(samples[metric]) for metric in required}
throughput = med["throughput"] / 1000.0
bandwidth = med["bandwidth"]
latency = med["latency"]
latency_p95 = med["latency_p95"]
latency_p99 = med["latency_p99"]
throughput_text = f"{throughput:8.3f} Kmsg/s"
print(
    f"{indent}| {size + 'B':<8} | {throughput_text:>16} | {bandwidth:>10.3f} MB/s |"
    f" {latency:>9.3f} ms | {latency_p95:>9.3f} ms | {latency_p99:>9.3f} ms |"
)
PY
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
        if row[0] != "RESULT" or row[1] != "dotnet":
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
    --build-dir)
      BUILD_DIR="${2:-}"
      shift
      ;;
    --reuse-build)
      REUSE_BUILD=1
      ;;
    --clean-build)
      CLEAN_BUILD=1
      ;;
    --output)
      shift
      ;;
    --pin-cpu)
      ;;
    --io-threads|--hwm|--send-hwm|--recv-hwm|--buf|--sndbuf|--rcvbuf|--sndtimeo|--rcvtimeo|--send-timeout-ms|--recv-timeout-ms|--auto-hwm-profile)
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

if [[ "${REUSE_BUILD}" -eq 1 && "${CLEAN_BUILD}" -eq 1 ]]; then
  echo "--reuse-build and --clean-build are mutually exclusive." >&2
  exit 1
fi

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
trap 'status=$?; cleanup_tmp_dir; print_total_time "${status}"' EXIT
FAILURES_FILE="${TMP_DIR}/failures.csv"
RESULT_DATA_FILE="${TMP_DIR}/result_data.csv"
: > "${FAILURES_FILE}"
: > "${RESULT_DATA_FILE}"

record_failure() {
  local pattern="${1:-}"
  local transport="${2:-}"
  local size="${3:-}"
  local run_index="${4:-}"
  local reason="${5:-}"
  printf '%s,%s,%s,%s,%s\n' \
    "${pattern}" "${transport}" "${size}" "${run_index}" "${reason}" >> "${FAILURES_FILE}"
}

platform="$(normalize_platform)"
timestamp="$(date +%Y%m%d_%H%M%S)"
report_base="perf_dotnet_single_${platform}_${timestamp}"
if [[ -n "${RESULTS_TAG}" ]]; then
  report_base="${report_base}_${RESULTS_TAG}"
fi
REPORT="${RESULTS_ROOT}/single/report/${report_base}.txt"
mkdir -p "${RESULTS_ROOT}/single/report"
prune_report_dir "${RESULTS_ROOT}/single/report"

ensure_build_output
prepare_core_runtime

# PERF_POLICY / ITEM 1: report emission is delegated to run_emit.py so the
# dotnet single report is byte-identical to the C canonical single emitter
# (bindings/c/perf/single/run_comparison.py). The benchmark .cs sources keep
# all prior dotnet fixes (8 audit fixes + C-faithful blocking-first-recv /
# DONTWAIT burst-drain / in-flight credit cap); this script only builds,
# prepares the core runtime, and drives the byte-faithful emitter.
EMIT_ARGS=("${PATTERN}" "--runs" "${RUNS}" "--results-dir" "${RESULTS_ROOT}" "--result-file" "${REPORT}")
if [[ -n "${RESULTS_TAG}" ]]; then
  EMIT_ARGS+=("--results-tag" "${RESULTS_TAG}")
fi

EMIT_ENV=(
  "PERF_SINGLE_DURATION_SECONDS=${DURATION}"
  "PERF_CONFIGURATION=${CONFIGURATION}"
)
if [[ -n "${TRANSPORTS}" ]]; then
  EMIT_ENV+=("PERF_TRANSPORTS=${TRANSPORTS}")
fi
if [[ -n "${MSG_SIZES}" ]]; then
  EMIT_ENV+=("PERF_MSG_SIZES=${MSG_SIZES}")
fi

set +e
env "${EMIT_ENV[@]}" python3 "${EMITTER}" "${EMIT_ARGS[@]}"
status=$?
set -e

SHOW_TOTAL_TIME=1
exit "${status}"
