#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

IS_WINDOWS=0
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    IS_WINDOWS=1
    ;;
esac

if [[ "${IS_WINDOWS}" -eq 1 ]]; then
  BUILD_DIR="${ROOT_DIR}/build/windows-x64"
else
  BUILD_DIR="${ROOT_DIR}/build/bench"
fi
PATTERN="ALL"
OUTPUT_FILE=""
RUNS=3
REUSE_BUILD=0
NO_TASKSET=0
BENCH_IO_THREADS=""
CORE_SIZES=0

usage() {
  cat <<'USAGE'
Usage: benchwithzmq/run_benchmarks_zmq_ab.sh [options]

Options:
  -h, --help            Show this help.
  --pattern NAME       Benchmark pattern (e.g., PAIR, PUBSUB, DEALER_DEALER).
  --build-dir PATH     Build directory (default: build/bench).
  --output PATH        Tee results to a file.
  --runs N             Iterations per configuration (default: 3).
  --reuse-build        Reuse existing build dir without re-running CMake.
  --no-taskset         Disable taskset CPU pinning on Linux.
  --io-threads N       Set BENCH_IO_THREADS for the benchmark run.
  --core-sizes         Use core sizes only (64,1024,65536).
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
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
    --runs)
      RUNS="${2:-}"
      shift
      ;;
    --no-taskset)
      NO_TASKSET=1
      ;;
    --io-threads)
      BENCH_IO_THREADS="${2:-}"
      shift
      ;;
    --core-sizes)
      CORE_SIZES=1
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
      -DZLINK_CXX_STANDARD=20
  else
    cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_BENCHMARKS=ON \
      -DZLINK_CXX_STANDARD=20
  fi

  if [[ "${IS_WINDOWS}" -eq 1 ]]; then
    cmake --build "${BUILD_DIR}" --config Release
  else
    cmake --build "${BUILD_DIR}"
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

RUN_CMD=("${PYTHON_BIN[@]}" "${ROOT_DIR}/benchwithzmq/run_comparison_zmq_ab.py" "${PATTERN}" --build-dir "${BUILD_DIR}" --runs "${RUNS}")
RUN_ENV=()
if [[ "${NO_TASKSET}" -eq 1 ]]; then
  RUN_ENV+=(BENCH_NO_TASKSET=1)
fi
if [[ -n "${BENCH_IO_THREADS}" ]]; then
  RUN_ENV+=(BENCH_IO_THREADS="${BENCH_IO_THREADS}")
fi
if [[ "${CORE_SIZES}" -eq 1 ]]; then
  RUN_ENV+=(BENCH_MSG_SIZES="64,1024,65536")
fi

if [[ -n "${OUTPUT_FILE}" ]]; then
  mkdir -p "$(dirname "${OUTPUT_FILE}")"
  env "${RUN_ENV[@]}" "${RUN_CMD[@]}" | tee "${OUTPUT_FILE}"
else
  env "${RUN_ENV[@]}" "${RUN_CMD[@]}"
fi
