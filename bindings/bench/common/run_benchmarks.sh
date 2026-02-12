#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

# Keep core benchmark build deterministic. Virtualenv/host toolchain flags can
# inject incompatible defines/includes and break C++ standard headers.
if [[ "${BENCH_PRESERVE_TOOLCHAIN_ENV:-0}" != "1" ]]; then
  unset CFLAGS CXXFLAGS CPPFLAGS LDFLAGS CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH LIBRARY_PATH
fi
SUPPORTED_BINDINGS=(python node dotnet java cpp)
DEFAULT_BINDINGS=(node dotnet java cpp)
ALL_BINDINGS=("${DEFAULT_BINDINGS[@]}")

IS_WINDOWS=0
PLATFORM="linux"
ARCH="x64"
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    IS_WINDOWS=1
    PLATFORM="windows"
    ;;
  Darwin*)
    PLATFORM="macos"
    ;;
  Linux*)
    PLATFORM="linux"
    ;;
esac

case "$(uname -m)" in
  x86_64|amd64) ARCH="x64" ;;
  aarch64|arm64) ARCH="arm64" ;;
  *) ARCH="$(uname -m)" ;;
esac

if [[ "${IS_WINDOWS}" -eq 1 ]]; then
  BUILD_DIR="${ROOT_DIR}/core/build/windows-x64"
else
  BUILD_DIR="${ROOT_DIR}/core/build/${PLATFORM}-${ARCH}"
fi

PATTERN="ALL"
WITH_BASELINE=0
OUTPUT_FILE=""
RUNS=1
REUSE_BUILD=1
BINDINGS_ONLY=0
PIN_CPU=0
ALLOW_CORE_FALLBACK=0
BENCH_IO_THREADS=""
BENCH_MSG_SIZES=""
BENCH_TRANSPORTS=""
RESULTS=1
RESULTS_DIR=""
RESULTS_TAG=""
BINDINGS_CSV="${BINDING:-DEFAULT}"

usage() {
  cat <<USAGE
Usage: bindings/bench/common/run_benchmarks.sh [options]

Compare cached zlink(core current) vs selected binding benchmark results.
Note: PATTERN=ALL runs all core benchwithzlink patterns.
Default bindings: ${DEFAULT_BINDINGS[*]} (python excluded by default).
ALL also maps to: ${ALL_BINDINGS[*]}.

Options:
  -h, --help            Show this help.
  --bindings LIST       Comma-separated bindings (default: ${DEFAULT_BINDINGS[*]}).
                       ALL maps to ${ALL_BINDINGS[*]}.
  --binding NAME        Convenience alias for --bindings NAME.
  --with-baseline       Refresh zlink(core) cache.
  --skip-libzlink       Do not refresh zlink(core) cache; use existing cache only (default).
  --pattern NAME        Benchmark pattern (e.g., PAIR).
  --build-dir PATH      Core benchmark build directory (default: core/build/<platform>-<arch>).
  --output PATH         Tee results to a file.
  --result              Write results under bindings/<binding>/benchwithzlink/results/YYYYMMDD/.
  --results-dir PATH    Override results root directory.
  --results-tag NAME    Optional tag appended to the results filename.
  --runs N              Iterations per configuration (default: 1).
  --bindings-only       Run only binding benchmarks (no zlink compare).
  --reuse-build         Reuse existing core build dir without re-running CMake (default: on).
  --no-reuse-build      Force clean configure/build of core benchmark binaries.
  --pin-cpu             Pin CPU core during benchmarks (Linux taskset).
  --allow-core-fallback Allow core fallback if binding runner returns no RESULT rows.
  --io-threads N        Set BENCH_IO_THREADS for the benchmark run.
  --msg-sizes LIST      Comma-separated message sizes (e.g., 1024 or 64,1024,65536).
  --size N              Convenience alias for --msg-sizes N.
  --transports LIST     Comma-separated transports (e.g., tcp,ws,inproc).
  --transport NAME      Convenience alias for --transports NAME.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --bindings) BINDINGS_CSV="${2:-}"; shift ;;
    --binding) BINDINGS_CSV="${2:-}"; shift ;;
    --with-baseline) WITH_BASELINE=1 ;;
    --skip-libzlink) WITH_BASELINE=0 ;;
    --with-libzlink) WITH_BASELINE=1 ;;
    --pattern) PATTERN="${2:-}"; shift ;;
    --reuse-build) REUSE_BUILD=1 ;;
    --no-reuse-build) REUSE_BUILD=0 ;;
    --build-dir) BUILD_DIR="${2:-}"; shift ;;
    --output) OUTPUT_FILE="${2:-}"; shift ;;
    --result|--baseline) RESULTS=1 ;;
    --results-dir|--baseline-dir) RESULTS_DIR="${2:-}"; shift ;;
    --results-tag|--baseline-tag) RESULTS_TAG="${2:-}"; shift ;;
    --runs) RUNS="${2:-}"; shift ;;
    --bindings-only) BINDINGS_ONLY=1 ;;
    --pin-cpu) PIN_CPU=1 ;;
    --allow-core-fallback) ALLOW_CORE_FALLBACK=1 ;;
    --io-threads) BENCH_IO_THREADS="${2:-}"; shift ;;
    --msg-sizes) BENCH_MSG_SIZES="${2:-}"; shift ;;
    --size) BENCH_MSG_SIZES="${2:-}"; shift ;;
    --transports|--transport) BENCH_TRANSPORTS="${2:-}"; shift ;;
    -h|--help) usage; exit 0 ;;
    *)
      if [[ "$1" != --* ]]; then
        if [[ -z "${PATTERN}" || "${PATTERN}" == "ALL" ]]; then
          PATTERN="$1"
        else
          PATTERN="${PATTERN},$1"
        fi
      else
        echo "Unknown option: $1" >&2
        usage >&2
        exit 1
      fi
      ;;
  esac
  shift
done

declare -a SELECTED_BINDINGS=()
if [[ -z "${BINDINGS_CSV}" || "${BINDINGS_CSV^^}" == "DEFAULT" ]]; then
  SELECTED_BINDINGS=("${DEFAULT_BINDINGS[@]}")
elif [[ "${BINDINGS_CSV^^}" == "ALL" ]]; then
  SELECTED_BINDINGS=("${ALL_BINDINGS[@]}")
else
  IFS=',' read -r -a _requested <<< "${BINDINGS_CSV}"
  for b in "${_requested[@]}"; do
    b="$(printf '%s' "${b}" | tr '[:upper:]' '[:lower:]' | xargs)"
    [[ -z "${b}" ]] && continue
    valid=0
    for s in "${SUPPORTED_BINDINGS[@]}"; do
      if [[ "${b}" == "${s}" ]]; then
        valid=1
        break
      fi
    done
    if [[ "${valid}" -ne 1 ]]; then
      echo "Unsupported binding: ${b}" >&2
      echo "Supported bindings: ${SUPPORTED_BINDINGS[*]}" >&2
      exit 1
    fi
    SELECTED_BINDINGS+=("${b}")
  done
fi
if [[ "${#SELECTED_BINDINGS[@]}" -eq 0 ]]; then
  echo "No bindings selected." >&2
  exit 1
fi

if [[ -z "${PATTERN}" ]]; then
  echo "Pattern name is required." >&2
  usage >&2
  exit 1
fi
if [[ "${PATTERN}" != "ALL" ]]; then
  PATTERN="$(printf '%s' "${PATTERN}" | tr '[:lower:]' '[:upper:]')"
fi
if [[ -z "${RUNS}" || ! "${RUNS}" =~ ^[0-9]+$ || "${RUNS}" -lt 1 ]]; then
  echo "Runs must be a positive integer." >&2
  exit 1
fi
if [[ -n "${BENCH_MSG_SIZES}" && ! "${BENCH_MSG_SIZES}" =~ ^[0-9]+(,[0-9]+)*$ ]]; then
  echo "BENCH_MSG_SIZES must be a comma-separated list of integers." >&2
  exit 1
fi
if [[ -n "${BENCH_TRANSPORTS}" && ! "${BENCH_TRANSPORTS}" =~ ^[a-z]+(,[a-z]+)*$ ]]; then
  echo "BENCH_TRANSPORTS must be a comma-separated list of names." >&2
  exit 1
fi

if [[ "${RESULTS}" -eq 1 ]]; then
  if [[ -n "${OUTPUT_FILE}" ]]; then
    echo "Error: --result cannot be used with --output." >&2
    exit 1
  fi
fi

BUILD_DIR="$(realpath -m "${BUILD_DIR}")"

if [[ "${REUSE_BUILD}" -eq 0 ]]; then
  echo "Preparing core benchmark binaries in ${BUILD_DIR}"
  rm -rf "${BUILD_DIR}"
  if [[ "${IS_WINDOWS}" -eq 1 ]]; then
    CMAKE_GENERATOR="${CMAKE_GENERATOR:-Visual Studio 17 2022}"
    CMAKE_ARCH="${CMAKE_ARCH:-x64}"
    cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G "${CMAKE_GENERATOR}" -A "${CMAKE_ARCH}" -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCHMARKS=ON -DZLINK_BUILD_BENCH_ZMQ=OFF -DZLINK_BUILD_BENCH_BEAST=OFF -DZLINK_CXX_STANDARD=17
  else
    cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCHMARKS=ON -DZLINK_BUILD_BENCH_ZMQ=OFF -DZLINK_BUILD_BENCH_BEAST=OFF -DZLINK_CXX_STANDARD=17
  fi
fi
if [[ "${IS_WINDOWS}" -eq 1 ]]; then
  cmake --build "${BUILD_DIR}" --config Release
else
  cmake --build "${BUILD_DIR}"
fi

if command -v python3 >/dev/null 2>&1; then
  PYTHON_BIN=(python3)
elif command -v python >/dev/null 2>&1; then
  PYTHON_BIN=(python)
else
  echo "Python not found." >&2
  exit 1
fi

if [[ -n "${OUTPUT_FILE}" ]]; then
  : > "${OUTPUT_FILE}"
fi

COMMON_CACHE_FILE="${ROOT_DIR}/bindings/bench/common/zlink_cache_${PLATFORM}-${ARCH}.json"
BASELINE_REFRESHED=0

if [[ "${BINDINGS_ONLY}" -eq 0 && "${WITH_BASELINE}" -eq 0 && ! -f "${COMMON_CACHE_FILE}" ]]; then
  for b in python node dotnet java cpp; do
    LEGACY_CACHE_FILE="${ROOT_DIR}/bindings/${b}/benchwithzlink/zlink_cache_${PLATFORM}-${ARCH}.json"
    if [[ -f "${LEGACY_CACHE_FILE}" ]]; then
      mkdir -p "$(dirname "${COMMON_CACHE_FILE}")"
      cp "${LEGACY_CACHE_FILE}" "${COMMON_CACHE_FILE}"
      echo "Migrated baseline cache: ${LEGACY_CACHE_FILE} -> ${COMMON_CACHE_FILE}"
      break
    fi
  done
fi

for binding in "${SELECTED_BINDINGS[@]}"; do
  echo ""
  echo "=== Binding: ${binding} ==="

  "${SCRIPT_DIR}/build_binding_runner.sh" "${binding}" "${ROOT_DIR}"

  RUN_CMD=("${PYTHON_BIN[@]}" "${SCRIPT_DIR}/run_binding_comparison.py" "${PATTERN}" --binding "${binding}" --build-dir "${BUILD_DIR}" --runs "${RUNS}")
  RUN_ENV=()
  [[ -n "${BENCH_IO_THREADS}" ]] && RUN_ENV+=(BENCH_IO_THREADS="${BENCH_IO_THREADS}")
  [[ -n "${BENCH_MSG_SIZES}" ]] && RUN_ENV+=(BENCH_MSG_SIZES="${BENCH_MSG_SIZES}")
  [[ -n "${BENCH_TRANSPORTS}" ]] && RUN_ENV+=(BENCH_TRANSPORTS="${BENCH_TRANSPORTS}")
  [[ "${PIN_CPU}" -eq 1 ]] && RUN_CMD+=(--pin-cpu)
  [[ "${ALLOW_CORE_FALLBACK}" -eq 1 ]] && RUN_CMD+=(--allow-core-fallback)

  if [[ "${BINDINGS_ONLY}" -eq 1 ]]; then
    RUN_CMD+=(--bindings-only)
  else
    if [[ "${WITH_BASELINE}" -eq 1 ]]; then
      if [[ "${BASELINE_REFRESHED}" -eq 0 ]]; then
        RUN_CMD+=(--refresh-baseline)
      fi
    else
      if [[ ! -f "${COMMON_CACHE_FILE}" ]]; then
        echo "Baseline cache not found: ${COMMON_CACHE_FILE}" >&2
        echo "Run with --with-baseline once to generate zlink cache." >&2
        exit 1
      fi
    fi
  fi

  if [[ -n "${OUTPUT_FILE}" ]]; then
    {
      echo ""
      echo "=== Binding: ${binding} ==="
      env "${RUN_ENV[@]}" "${RUN_CMD[@]}"
    } | tee -a "${OUTPUT_FILE}"
  elif [[ "${RESULTS}" -eq 1 ]]; then
    local_results_dir="${RESULTS_DIR}"
    if [[ -z "${local_results_dir}" ]]; then
      local_results_dir="${ROOT_DIR}/bindings/${binding}/benchwithzlink/results"
    fi
    DATE_DIR="$(date +%Y%m%d)"
    TS="$(date +%Y%m%d_%H%M%S)"
    NAME="bench_${binding}_${PLATFORM}_${PATTERN}_${TS}"
    if [[ -n "${RESULTS_TAG}" ]]; then
      NAME="${NAME}_${RESULTS_TAG}"
    fi
    binding_output="${local_results_dir}/${DATE_DIR}/${NAME}.txt"
    mkdir -p "$(dirname "${binding_output}")"
    env "${RUN_ENV[@]}" "${RUN_CMD[@]}" | tee "${binding_output}"
  else
    env "${RUN_ENV[@]}" "${RUN_CMD[@]}"
  fi

  if [[ "${BINDINGS_ONLY}" -eq 0 && "${WITH_BASELINE}" -eq 1 ]]; then
    BASELINE_REFRESHED=1
  fi
done
