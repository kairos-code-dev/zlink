#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Repo root (three levels above: core/bench/benchwithzmq)
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

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
  x86_64|amd64)
    ARCH="x64"
    ;;
  aarch64|arm64)
    ARCH="arm64"
    ;;
  *)
    ARCH="$(uname -m)"
    ;;
esac

if [[ "${IS_WINDOWS}" -eq 1 ]]; then
  BUILD_DIR="${ROOT_DIR}/core/build/windows-x64"
else
  BUILD_DIR="${ROOT_DIR}/core/build/${PLATFORM}-${ARCH}"
fi
PATTERN="ALL"
WITH_LIBZMQ=1
OUTPUT_FILE=""
RUNS=1
REUSE_BUILD=0
ZLINK_ONLY=0
PIN_CPU=0
BENCH_IO_THREADS=""
BENCH_MSG_SIZES=""
BENCH_TRANSPORTS=""
RESULTS=1
RESULTS_DIR=""
RESULTS_TAG=""

usage() {
  cat <<'USAGE'
Usage: core/bench/benchwithzmq/run_benchmarks.sh [options]

Options:
  -h, --help            Show this help.
  --skip-libzmq        Skip libzmq baseline run (uses existing cache).
  --with-libzmq        Run libzmq baseline and refresh cache (default).
  --pattern NAME       Benchmark pattern (e.g., PAIR, PUBSUB, DEALER_DEALER, STREAM).
  --build-dir PATH     Build directory (default: core/build/<platform>-<arch>).
  --output PATH        Tee results to a file.
  --result             Write results under core/bench/benchwithzmq/results/YYYYMMDD/.
  --results-dir PATH   Override results root directory.
  --results-tag NAME   Optional tag appended to the results filename.
  --runs N             Iterations per configuration (default: 1).
  --zlink-only         Run only zlink benchmarks (no libzmq baseline).
  --reuse-build        Reuse existing build dir without re-running CMake.
  --pin-cpu            Pin CPU core during benchmarks (Linux taskset).
  --io-threads N       Set BENCH_IO_THREADS for the benchmark run.
  --msg-sizes LIST     Comma-separated message sizes (e.g., 1024 or 64,1024,65536).
  --size N             Convenience alias for --msg-sizes N.
  --transport LIST     Comma-separated transports (e.g., tcp or tcp,inproc,ipc).
                       STREAM pattern runs on tcp only.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --skip-libzmq)
      WITH_LIBZMQ=0
      ;;
    --with-libzmq)
      WITH_LIBZMQ=1
      ;;
    --pattern)
      PATTERN="${2:-}"
      shift
      ;;
    --reuse-build)
      REUSE_BUILD=1
      ;;
    --build-dir)
      BUILD_DIR="${2:-}"
      shift
      ;;
    --output)
      OUTPUT_FILE="${2:-}"
      shift
      ;;
    --result)
      RESULTS=1
      ;;
    --results-dir)
      RESULTS_DIR="${2:-}"
      shift
      ;;
    --results-tag)
      RESULTS_TAG="${2:-}"
      shift
      ;;
    --runs)
      RUNS="${2:-}"
      shift
      ;;
    --zlink-only)
      ZLINK_ONLY=1
      ;;
    --pin-cpu)
      PIN_CPU=1
      ;;
    --io-threads)
      BENCH_IO_THREADS="${2:-}"
      shift
      ;;
    --msg-sizes)
      BENCH_MSG_SIZES="${2:-}"
      shift
      ;;
    --size)
      BENCH_MSG_SIZES="${2:-}"
      shift
      ;;
    --transport)
      BENCH_TRANSPORTS="${2:-}"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
  shift
done

if [[ -z "${PATTERN}" ]]; then
  echo "Pattern name is required." >&2
  usage >&2
  exit 1
fi

# Normalize pattern to uppercase for consistent matching
if [[ "${PATTERN}" != "ALL" ]]; then
  PATTERN="$(printf '%s' "${PATTERN}" | tr '[:lower:]' '[:upper:]')"
fi

if [[ -z "${RUNS}" || ! "${RUNS}" =~ ^[0-9]+$ || "${RUNS}" -lt 1 ]]; then
  echo "Runs must be a positive integer." >&2
  usage >&2
  exit 1
fi

if [[ -n "${BENCH_IO_THREADS}" && ! "${BENCH_IO_THREADS}" =~ ^[0-9]+$ ]]; then
  echo "BENCH_IO_THREADS must be a positive integer." >&2
  usage >&2
  exit 1
fi

if [[ -n "${BENCH_MSG_SIZES}" && ! "${BENCH_MSG_SIZES}" =~ ^[0-9]+(,[0-9]+)*$ ]]; then
  echo "BENCH_MSG_SIZES must be a comma-separated list of integers." >&2
  usage >&2
  exit 1
fi
if [[ -n "${BENCH_TRANSPORTS}" && ! "${BENCH_TRANSPORTS}" =~ ^[a-zA-Z0-9]+(,[a-zA-Z0-9]+)*$ ]]; then
  echo "BENCH_TRANSPORTS must be a comma-separated list of names." >&2
  usage >&2
  exit 1
fi

if [[ "${RESULTS}" -eq 1 ]]; then
  if [[ -n "${OUTPUT_FILE}" ]]; then
    echo "Error: --result cannot be used with --output." >&2
    exit 1
  fi
  if [[ -z "${RESULTS_DIR}" ]]; then
    RESULTS_DIR="${SCRIPT_DIR}/results"
  fi
  DATE_DIR="$(date +%Y%m%d)"
  TS="$(date +%Y%m%d_%H%M%S)"
  NAME="bench_${PLATFORM}_${PATTERN}_${TS}"
  if [[ -n "${RESULTS_TAG}" ]]; then
    NAME="${NAME}_${RESULTS_TAG}"
  fi
  OUTPUT_FILE="${RESULTS_DIR}/${DATE_DIR}/${NAME}.txt"
fi

BUILD_DIR="$(realpath -m "${BUILD_DIR}")"
ROOT_DIR="$(realpath -m "${ROOT_DIR}")"

if [[ "${BUILD_DIR}" != "${ROOT_DIR}/"* ]]; then
  echo "Build directory must be inside repo root: ${ROOT_DIR}" >&2
  exit 1
fi

if [[ "${REUSE_BUILD}" -eq 1 ]]; then
  echo "Reusing build directory: ${BUILD_DIR}"
  if [[ ! -d "${BUILD_DIR}" ]]; then
    echo "Error: build directory ${BUILD_DIR} does not exist" >&2
    exit 1
  fi
else
  echo "Cleaning build directory: ${BUILD_DIR}"
  rm -rf "${BUILD_DIR}"

  if [[ "${IS_WINDOWS}" -eq 1 ]]; then
    CMAKE_GENERATOR="${CMAKE_GENERATOR:-Visual Studio 17 2022}"
    CMAKE_ARCH="${CMAKE_ARCH:-x64}"
    cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
      -G "${CMAKE_GENERATOR}" \
      -A "${CMAKE_ARCH}" \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_BENCHMARKS=ON \
      -DZLINK_BUILD_BENCH_ZLINK=OFF \
      -DZLINK_BUILD_BENCH_BEAST=OFF \
      -DZLINK_CXX_STANDARD=17
  else
    cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_BENCHMARKS=ON \
      -DZLINK_BUILD_BENCH_ZLINK=OFF \
      -DZLINK_BUILD_BENCH_BEAST=OFF \
      -DZLINK_CXX_STANDARD=17
  fi
fi

if [[ "${IS_WINDOWS}" -eq 1 ]]; then
  cmake --build "${BUILD_DIR}" --config Release
else
  cmake --build "${BUILD_DIR}"
fi

if [[ "${IS_WINDOWS}" -eq 0 ]]; then
  LIBZMQ_LIB_DIR="${ROOT_DIR}/core/bench/benchwithzmq/libzmq/libzmq_dist/${PLATFORM}-${ARCH}/lib"
  if [[ -f "${LIBZMQ_LIB_DIR}/libzmq.so" && ! -e "${LIBZMQ_LIB_DIR}/libzmq.so.5" ]]; then
    ln -sf "libzmq.so" "${LIBZMQ_LIB_DIR}/libzmq.so.5"
  fi
fi

PYTHON_BIN=()
if [[ "${IS_WINDOWS}" -eq 1 ]]; then
  if command -v py >/dev/null 2>&1; then
    PYTHON_BIN=(py -3)
  elif command -v python >/dev/null 2>&1; then
    PYTHON_BIN=(python)
  elif command -v python3 >/dev/null 2>&1; then
    PYTHON_BIN=(python3)
  else
    echo "Python not found. Install Python 3 or ensure it is on PATH." >&2
    exit 1
  fi
else
  if command -v python3 >/dev/null 2>&1; then
    PYTHON_BIN=(python3)
  elif command -v python >/dev/null 2>&1; then
    PYTHON_BIN=(python)
  else
    echo "Python not found. Install Python 3 or ensure it is on PATH." >&2
    exit 1
  fi
fi

RUN_CMD=("${PYTHON_BIN[@]}" "${ROOT_DIR}/core/bench/benchwithzmq/run_comparison.py" "${PATTERN}" --build-dir "${BUILD_DIR}" --runs "${RUNS}")
RUN_ENV=()
if [[ "${PIN_CPU}" -eq 1 ]]; then
  RUN_ENV+=(BENCH_TASKSET=1)
fi
if [[ -n "${BENCH_IO_THREADS}" ]]; then
  RUN_ENV+=(BENCH_IO_THREADS="${BENCH_IO_THREADS}")
fi
if [[ -n "${BENCH_MSG_SIZES}" ]]; then
  RUN_ENV+=(BENCH_MSG_SIZES="${BENCH_MSG_SIZES}")
fi
if [[ -n "${BENCH_TRANSPORTS}" ]]; then
  RUN_ENV+=(BENCH_TRANSPORTS="${BENCH_TRANSPORTS}")
fi

if [[ "${ZLINK_ONLY}" -eq 1 ]]; then
  RUN_CMD+=(--zlink-only)
else
  if [[ "${WITH_LIBZMQ}" -eq 1 ]]; then
    RUN_CMD+=(--refresh-libzmq)
  else
    CACHE_FILE="${ROOT_DIR}/core/bench/benchwithzmq/libzmq_cache_${PLATFORM}-${ARCH}.json"
    if [[ ! -f "${CACHE_FILE}" ]]; then
      echo "libzmq cache not found: ${CACHE_FILE}" >&2
      echo "Run with --with-libzmq once to generate the baseline." >&2
      exit 1
    fi
  fi
fi

if [[ -n "${OUTPUT_FILE}" ]]; then
  mkdir -p "$(dirname "${OUTPUT_FILE}")"
  env "${RUN_ENV[@]}" "${RUN_CMD[@]}" | tee "${OUTPUT_FILE}"
else
  env "${RUN_ENV[@]}" "${RUN_CMD[@]}"
fi
