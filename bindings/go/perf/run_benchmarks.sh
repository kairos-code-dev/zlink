#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_DIR="$(cd "${ROOT_DIR}/../.." && pwd)"
cd "${ROOT_DIR}"

export GOCACHE="${GOCACHE:-/tmp/zlink-go-cache}"
export GOTMPDIR="${GOTMPDIR:-/tmp/zlink-go-tmp}"
mkdir -p "${GOCACHE}" "${GOTMPDIR}"

VERSION_FILE="${REPO_DIR}/VERSION"
CORE_LIB_DIR="${REPO_DIR}/core/build/lib"
CORE_VERSION="$(awk -F= '/^LIBZLINK_VERSION=/{print $2}' "${VERSION_FILE}")"
CORE_LIB="${CORE_LIB_DIR}/libzlink.so.${CORE_VERSION}"
PERF_REPORT_PY="${REPO_DIR}/bindings/python/perf/perf_report.py"
TOTAL_TIME_ENABLED=0

print_total_time() {
  local status="${1:-0}"
  if [[ "${TOTAL_TIME_ENABLED}" -ne 1 ]]; then
    return
  fi
  local elapsed
  elapsed=$(($(date +%s) - START_SECONDS))
  echo "Total benchmark time: ${elapsed}s (${elapsed}s, exit=${status})"
}
PATTERN="ALL"
DURATION="5"
MSG_SIZES="${PERF_MSG_SIZES:-64,256,1024,65536,131072,262144}"
TRANSPORTS="${PERF_TRANSPORTS:-}"
RUNS="1"
RESULTS_DIR="${SCRIPT_DIR}/results/single/report"
RESULTS_TAG=""
OUTPUT_FILE=""
BUILD_DIR="${SCRIPT_DIR}/build/single"
REUSE_BUILD=0
CLEAN_BUILD=0
PIN_CPU="off"
IO_THREADS=""
HWM=""
SEND_HWM=""
RECV_HWM=""
SNDBUF=""
RCVBUF=""
SNDTIMEO_MS=""
RCVTIMEO_MS=""
AUTO_HWM_PROFILE=""
RUN_COOLDOWN_MS="${PERF_SINGLE_RUN_COOLDOWN_MS:-500}"

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

sleep_millis() {
  local millis="${1:-0}"
  if [[ ! "${millis}" =~ ^[0-9]+$ || "${millis}" -eq 0 ]]; then
    return
  fi
  sleep "$((millis / 1000)).$(printf '%03d' "$((millis % 1000))")"
}

is_positive_uint() {
  local value="${1:-}"
  [[ "${value}" =~ ^[0-9]+$ ]] && (( 10#${value} > 0 ))
}

go_gomaxprocs_floor4() {
  local value="${1:-}"
  if is_positive_uint "${value}"; then
    local numeric=$((10#${value}))
    if (( numeric > 4 )); then
      printf '%s\n' "${numeric}"
      return
    fi
  fi
  printf '4\n'
}

validate_go_gomaxprocs() {
  local source="$1"
  local value="${2:-}"
  if ! is_positive_uint "${value}"; then
    echo "Error: ${source} must be a positive integer for Go perf runs." >&2
    exit 1
  fi
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
  --buf SIZE
  --sndbuf SIZE
  --rcvbuf SIZE
  --sndtimeo N
  --rcvtimeo N
  --send-timeout-ms N
  --recv-timeout-ms N
  --auto-hwm-profile NAME
  -h, --help

Notes:
  - Supported single patterns: PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER,SPOT
  - If GOMAXPROCS is unset, PERF_GO_GOMAXPROCS is an explicit positive-integer override.
    Otherwise --io-threads/PERF_IO_THREADS derives Go scheduler parallelism
    with a minimum of 4.
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
      BUILD_DIR="$2"
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
    --buf)
      SNDBUF="$2"
      RCVBUF="$2"
      shift 2 ;;
    --sndbuf)
      SNDBUF="$2"
      shift 2 ;;
    --rcvbuf)
      RCVBUF="$2"
      shift 2 ;;
    --sndtimeo|--send-timeout-ms)
      SNDTIMEO_MS="$2"
      shift 2 ;;
    --rcvtimeo|--recv-timeout-ms)
      RCVTIMEO_MS="$2"
      shift 2 ;;
    --auto-hwm-profile)
      AUTO_HWM_PROFILE="$2"
      shift 2 ;;
    --reuse-build)
      REUSE_BUILD=1
      shift ;;
    --clean-build)
      CLEAN_BUILD=1
      shift ;;
    --pin-cpu)
      PIN_CPU="on"
      shift ;;
    *)
      echo "Error: unknown option $1" >&2
      exit 1 ;;
  esac
done

if [[ "${REUSE_BUILD}" -eq 1 && "${CLEAN_BUILD}" -eq 1 ]]; then
  echo "Error: --reuse-build and --clean-build are mutually exclusive." >&2
  exit 1
fi

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
if [[ -n "${SNDBUF}" ]]; then
  export PERF_SINGLE_SNDBUF="${SNDBUF}"
fi
if [[ -n "${RCVBUF}" ]]; then
  export PERF_SINGLE_RCVBUF="${RCVBUF}"
fi
if [[ -n "${SNDTIMEO_MS}" ]]; then
  export PERF_SINGLE_SNDTIMEO_MS="${SNDTIMEO_MS}"
fi
if [[ -n "${RCVTIMEO_MS}" ]]; then
  export PERF_SINGLE_RCVTIMEO_MS="${RCVTIMEO_MS}"
fi
if [[ -n "${AUTO_HWM_PROFILE}" ]]; then
  export PERF_CTX_AUTO_HWM_PROFILE="${AUTO_HWM_PROFILE}"
fi
GO_GOMAXPROCS_SOURCE="env:GOMAXPROCS"
if [[ -n "${GOMAXPROCS:-}" ]]; then
  validate_go_gomaxprocs "GOMAXPROCS" "${GOMAXPROCS}"
else
  if [[ -n "${PERF_GO_GOMAXPROCS:-}" ]]; then
    validate_go_gomaxprocs "PERF_GO_GOMAXPROCS" "${PERF_GO_GOMAXPROCS}"
    export GOMAXPROCS="${PERF_GO_GOMAXPROCS}"
    GO_GOMAXPROCS_SOURCE="PERF_GO_GOMAXPROCS"
  elif is_positive_uint "${IO_THREADS}"; then
    export GOMAXPROCS="$(go_gomaxprocs_floor4 "${IO_THREADS}")"
    GO_GOMAXPROCS_SOURCE="--io-threads"
  elif is_positive_uint "${PERF_IO_THREADS:-}"; then
    export GOMAXPROCS="$(go_gomaxprocs_floor4 "${PERF_IO_THREADS}")"
    GO_GOMAXPROCS_SOURCE="PERF_IO_THREADS"
  else
    export GOMAXPROCS="4"
    GO_GOMAXPROCS_SOURCE="default"
  fi
fi

case "$(uname -s)" in
  Linux*) PLATFORM="linux" ;;
  Darwin*) PLATFORM="macos" ;;
  *) PLATFORM="windows" ;;
esac

GO_SINGLE_BIN="${BUILD_DIR}/perf_single"

build_go_perf_binary() {
  if [[ "${CLEAN_BUILD}" -eq 1 ]]; then
    rm -rf "${BUILD_DIR}"
  fi
  mkdir -p "${BUILD_DIR}"
  if [[ "${REUSE_BUILD}" -eq 1 ]]; then
    if [[ ! -x "${GO_SINGLE_BIN}" ]]; then
      echo "existing Go single perf binary not found for --reuse-build: ${GO_SINGLE_BIN}" >&2
      exit 1
    fi
    return
  fi
  go build -o "${GO_SINGLE_BIN}" ./perf/single
}

run_go_perf() {
  local package="$1"
  shift
  if [[ "${package}" != "./perf/single" ]]; then
    echo "Error: unsupported Go perf package: ${package}" >&2
    return 1
  fi
  if [[ "${PIN_CPU}" != "on" ]]; then
    "${GO_SINGLE_BIN}" "$@"
    return
  fi

  case "$(uname -s)" in
    Linux*)
      if ! command -v taskset >/dev/null 2>&1; then
        echo "Error: --pin-cpu requires taskset on Linux" >&2
        return 1
      fi
      taskset -c 0 "${GO_SINGLE_BIN}" "$@"
      ;;
    *)
      echo "Error: --pin-cpu is not supported by this runner on $(uname -s)" >&2
      return 1
      ;;
  esac
}

prepare_core_runtime() {
  if [[ ! -f "${CORE_LIB}" ]]; then
    echo "core runtime not found: ${CORE_LIB}" >&2
    echo "Build core/build before running Go perf." >&2
    exit 1
  fi
  local newer_source
  newer_source="$(
    find "${REPO_DIR}/core/src" "${REPO_DIR}/core/include" \
      -type f -newer "${CORE_LIB}" -print -quit 2>/dev/null || true
  )"
  if [[ -n "${newer_source}" ]]; then
    echo "Error: stale core runtime detected for bindings/go/perf." >&2
    echo "  runtime: ${CORE_LIB}" >&2
    echo "  newer source: ${newer_source}" >&2
    echo "Rebuild core/build before running run_benchmarks.sh." >&2
    exit 1
  fi
  echo "Perf core build dir: ${REPO_DIR}/core/build"
  echo "Perf runtime libzlink: ${CORE_LIB}"
  export LD_LIBRARY_PATH="${CORE_LIB_DIR}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
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
prepare_core_runtime
build_go_perf_binary
START_SECONDS="$(date +%s)"
TOTAL_TIME_ENABLED=1
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/zlink-go-single.XXXXXX")"
RAW_RESULTS_FILE="${TMP_DIR}/result_data.log"
cleanup() {
  local status=$?
  rm -rf "${TMP_DIR}"
  print_total_time "${status}"
}
trap cleanup EXIT

IFS=',' read -r -a SIZES <<< "${MSG_SIZES}"
if [[ "${PATTERN}" == "ALL" ]]; then
  PATTERNS=("PAIR" "PUBSUB" "DEALER_DEALER" "DEALER_ROUTER" "ROUTER_ROUTER" "SPOT")
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
    SPOT) echo "tcp tls ws wss" ;;
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
effective_or_auto() {
  local value="$1"
  local fallback="${2:-auto-hwm}"
  if [[ -n "${value}" ]]; then
    printf '%s\n' "${value}"
  else
    printf '%s\n' "${fallback}"
  fi
}

emit_effective_options_single() {
  local section="$1"
  echo "## Effective Options (${section})"
  echo "- lang: go"
  echo "- suite: single"
  echo "- runs: ${RUNS}"
  echo "- duration_seconds: ${DURATION}"
  echo "- timeout_seconds: ${PERF_SINGLE_TIMEOUT_SECONDS:-30}"
  echo "- fail_fast: ${PERF_FAIL_FAST:-0}"
  echo "- io_threads: ${IO_THREADS:-${PERF_IO_THREADS:-1}}"
  echo "- go_gomaxprocs: ${GOMAXPROCS:-unset}"
  echo "- go_gomaxprocs_source: ${GO_GOMAXPROCS_SOURCE}"
  echo "- hwm: $(effective_or_auto "${HWM}")"
  echo "- sndhwm: $(effective_or_auto "${SEND_HWM:-${HWM}}")"
  echo "- rcvhwm: $(effective_or_auto "${RECV_HWM:-${HWM}}")"
  echo "- sndbuf: $(effective_or_auto "${SNDBUF}")"
  echo "- rcvbuf: $(effective_or_auto "${RCVBUF}")"
  echo "- sndtimeo_ms: ${SNDTIMEO_MS:-${PERF_SINGLE_SNDTIMEO_MS:-200}}"
  echo "- rcvtimeo_ms: ${RCVTIMEO_MS:-${PERF_SINGLE_RCVTIMEO_MS:-200}}"
  echo "- ctx_auto_hwm_enable: ${PERF_CTX_AUTO_HWM_ENABLE:-core-default}"
  echo "- ctx_auto_hwm_profile: ${AUTO_HWM_PROFILE:-${PERF_SINGLE_CTX_AUTO_HWM_PROFILE:-${PERF_CTX_AUTO_HWM_PROFILE:-balanced}}}"
  echo "- patterns: ${EFFECTIVE_PATTERNS_CSV}"
  echo "- transports: ${EFFECTIVE_TRANSPORTS_CSV}"
  echo "- msg_sizes: ${MSG_SIZES}"
}

append_case_output() {
  local case_log="$1"
  cat "${case_log}" >> "${RAW_RESULTS_FILE}"
  if [[ -s "${case_log}" ]]; then
    printf '\n' >> "${RAW_RESULTS_FILE}"
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
  echo "      | Size     |       Throughput |    Bandwidth |  Lat.Mean(ms) |   Lat.P95(ms) |   Lat.P99(ms) |"
  echo "      |----------|------------------|--------------|--------------|--------------|--------------|"
}

progress_case_row() {
  local pattern="$1"
  local size="$2"
  local case_log="$3"
  if grep -Eq '^UNSUPPORTED,' "${case_log}"; then
    python3 "${PERF_REPORT_PY}" status-row --suite single --size "$size" --status unsupported
    return
  fi
  if grep -Eq '^SKIP,' "${case_log}"; then
    python3 "${PERF_REPORT_PY}" status-row --suite single --size "$size" --status skip
    return
  fi
  python3 "${PERF_REPORT_PY}" progress-case-row \
    --suite single \
    --pattern "$pattern" \
    --size "$size" \
    --case-log "$case_log"
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
  python3 "${PERF_REPORT_PY}" render-log-tables --suite single --tmp-dir "$TMP_DIR"
}

{
  emit_effective_options_single "start"
} > "${RESULTS_FILE}"
exec 3>&1
exec >/dev/null

result_lines=0
unsupported_cases=0
skip_cases=0
fail=0
expected_cases=0
stop_early=0
FAILURES=()

for run in $(seq 1 "${RUNS}"); do
  if [[ "${stop_early}" -eq 1 ]]; then
    break
  fi
  for pattern in "${PATTERNS[@]}"; do
    if [[ "${stop_early}" -eq 1 ]]; then
      break
    fi
    progress_pattern_heading "${pattern}"
    read -r -a PATTERN_XPORTS <<< "$(pattern_transports "${pattern}")"
    for transport in "${PATTERN_XPORTS[@]}"; do
      if [[ "${stop_early}" -eq 1 ]]; then
        break
      fi
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
        case_ok=0
        if run_go_perf ./perf/single \
          --pattern "${pattern}" \
          --transport "${transport}" \
          --msg-size "${size}" \
          --duration "${DURATION}" \
          > "${case_log}" 2>&1; then
          case_ok=1
        fi
        if [[ "${case_ok}" -eq 1 ]]; then
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
            echo "FAIL,current,${pattern},${transport},${size},no_result_lines" >> "${RAW_RESULTS_FILE}"
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
          echo "FAIL,current,${pattern},${transport},${size},exit_nonzero" >> "${RAW_RESULTS_FILE}"
          fail=$((fail + 1))
          FAILURES+=("${pattern} current ${transport} ${size}B: exit_nonzero")
        fi
        progress_table_header
        progress_case_row "${pattern}" "${size}" "${case_log}"
        if [[ "${PERF_FAIL_FAST:-0}" == "1" && "${fail}" -gt 0 ]]; then
          stop_early=1
          break
        fi
        sleep_millis "${RUN_COOLDOWN_MS}"
      done
      echo "    Testing ${transport}: Done"
    done
  done
done

table_output="$(render_tables)"
if [[ -n "${table_output}" ]]; then
  printf '%s\n' "${table_output}" >> "${RESULTS_FILE}"
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

{
  echo
  python3 "${PERF_REPORT_PY}" single-auto-hwm \
    --patterns "${EFFECTIVE_PATTERNS_CSV}" \
    --msg-sizes "${MSG_SIZES}" \
    --raw-results "${RAW_RESULTS_FILE}"
} >> "${RESULTS_FILE}"

expected_result_lines=$((expected_cases * 5))
status="partial"
if [[ "${fail}" -eq 0 && "${result_lines}" -eq "${expected_result_lines}" ]]; then
  status="complete"
fi

{
  expected_result_lines=$((expected_cases * 5))
  echo
  emit_effective_options_single "result"
  echo
  echo "## Result Data"
  if [[ -s "${RAW_RESULTS_FILE}" ]]; then
    grep -E '^(RESULT|UNSUPPORTED|SKIP|FAIL),' "${RAW_RESULTS_FILE}" || true
  fi
  echo
  echo "## Completion"
  echo "- status: ${status}"
  echo "- fail_fast_stopped: ${stop_early}"
  echo "- expected_result_lines: ${expected_result_lines}"
  echo "- actual_result_lines: ${result_lines}"
  echo
  echo "Saved result file: ${RESULTS_FILE} (status=${status})"
} >> "${RESULTS_FILE}"

if [[ -n "${OUTPUT_FILE}" ]]; then
  cp "${RESULTS_FILE}" "${OUTPUT_FILE}"
fi

exec >&3
cat "${RESULTS_FILE}"

if [[ "${status}" != "complete" ]]; then
  exit 1
fi
