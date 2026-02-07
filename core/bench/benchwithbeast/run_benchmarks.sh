#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
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
OUTPUT_FILE=""
RUNS=1
REUSE_BUILD=0
BENCH_MSG_SIZES=""
BENCH_TRANSPORTS=""
RESULTS=0
RESULTS_DIR=""
RESULTS_TAG=""

usage() {
  cat <<'USAGE'
Usage: core/bench/benchwithbeast/run_benchmarks.sh [options]

Options:
  -h, --help            Show this help.
  --build-dir PATH      Build directory (default: core/build/<platform>-<arch>).
  --output PATH         Tee results to a file.
  --result              Write results under core/bench/benchwithbeast/results/YYYYMMDD/.
  --results-dir PATH    Override results root directory.
  --results-tag NAME    Optional tag appended to the results filename.
  --runs N              Iterations per configuration (default: 1).
  --msg-sizes LIST      Comma-separated message sizes (e.g., 64 or 64,1024).
  --size N              Convenience alias for --msg-sizes N.
  --transports LIST     Comma-separated transports (e.g., tcp,ws,tls,wss).
  --transport NAME      Convenience alias for --transports NAME.
  --reuse-build         Reuse existing build dir without re-running CMake.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
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
    --msg-sizes|--size)
      BENCH_MSG_SIZES="${2:-}"
      shift
      ;;
    --transports|--transport)
      BENCH_TRANSPORTS="${2:-}"
      shift
      ;;
    --reuse-build)
      REUSE_BUILD=1
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

if [[ -z "${RUNS}" || ! "${RUNS}" =~ ^[0-9]+$ || "${RUNS}" -lt 1 ]]; then
  echo "Runs must be a positive integer." >&2
  usage >&2
  exit 1
fi

if [[ -n "${BENCH_MSG_SIZES}" && ! "${BENCH_MSG_SIZES}" =~ ^[0-9]+(,[0-9]+)*$ ]]; then
  echo "BENCH_MSG_SIZES must be a comma-separated list of integers." >&2
  usage >&2
  exit 1
fi

if [[ -n "${BENCH_TRANSPORTS}" && ! "${BENCH_TRANSPORTS}" =~ ^[a-z]+(,[a-z]+)*$ ]]; then
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
  NAME="bench_${PLATFORM}_BEAST_${TS}"
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
      -DZLINK_BUILD_BENCH_ZMQ=OFF \
      -DZLINK_BUILD_BENCH_ZLINK=OFF \
      -DZLINK_CXX_STANDARD=17
  else
    cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_BENCHMARKS=ON \
      -DZLINK_BUILD_BENCH_ZMQ=OFF \
      -DZLINK_BUILD_BENCH_ZLINK=OFF \
      -DZLINK_CXX_STANDARD=17
  fi
fi

if [[ "${IS_WINDOWS}" -eq 1 ]]; then
  cmake --build "${BUILD_DIR}" --config Release
else
  cmake --build "${BUILD_DIR}"
fi

BIN_DIR="${BUILD_DIR}/bin"
if [[ "${IS_WINDOWS}" -eq 1 ]]; then
  BIN_DIR="${BIN_DIR}/Release"
fi
BIN_PATH="${BIN_DIR}/bench_beast_stream"
if [[ "${IS_WINDOWS}" -eq 1 ]]; then
  BIN_PATH+=".exe"
fi
if [[ ! -x "${BIN_PATH}" ]]; then
  echo "bench_beast_stream not found at ${BIN_PATH}" >&2
  exit 1
fi

if [[ -z "${BENCH_MSG_SIZES}" ]]; then
  BENCH_MSG_SIZES="64"
fi
if [[ -z "${BENCH_TRANSPORTS}" ]]; then
  BENCH_TRANSPORTS="tcp"
fi

IFS=',' read -r -a SIZE_LIST <<< "${BENCH_MSG_SIZES}"
IFS=',' read -r -a TR_LIST <<< "${BENCH_TRANSPORTS}"

run_cmd() {
  local transport="$1"
  local size="$2"
  "${BIN_PATH}" "${transport}" "${size}" --runs "${RUNS}"
}

if [[ -n "${OUTPUT_FILE}" ]]; then
  mkdir -p "$(dirname "${OUTPUT_FILE}")"
  {
    for tr in "${TR_LIST[@]}"; do
      for sz in "${SIZE_LIST[@]}"; do
        run_cmd "${tr}" "${sz}"
      done
    done
  } | tee "${OUTPUT_FILE}"
else
  for tr in "${TR_LIST[@]}"; do
    for sz in "${SIZE_LIST[@]}"; do
      run_cmd "${tr}" "${sz}"
    done
  done
fi
