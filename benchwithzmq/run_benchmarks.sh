#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_DIR="${ROOT_DIR}/build-bench-asio"
PATTERN="ALL"
WITH_LIBZMQ=1
OUTPUT_FILE=""
RUNS=3

usage() {
  cat <<'USAGE'
Usage: benchwithzmq/run_benchmarks.sh [options]

Options:
  --skip-libzmq        Skip libzmq baseline run (uses existing cache).
  --with-libzmq        Run libzmq baseline and refresh cache (default).
  --pattern NAME       Benchmark pattern (e.g., PAIR, PUBSUB, DEALER_DEALER).
  --build-dir PATH     Build directory (default: build-bench-asio).
  --output PATH        Tee results to a file.
  --runs N             Iterations per configuration (default: 3).
  -h, --help           Show this help.
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

BUILD_DIR="$(realpath -m "${BUILD_DIR}")"
ROOT_DIR="$(realpath -m "${ROOT_DIR}")"

if [[ "${BUILD_DIR}" != "${ROOT_DIR}/"* ]]; then
  echo "Build directory must be inside repo root: ${ROOT_DIR}" >&2
  exit 1
fi

echo "Cleaning build directory: ${BUILD_DIR}"
rm -rf "${BUILD_DIR}"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_BENCHMARKS=ON \
  -DWITH_BOOST_ASIO=ON \
  -DZMQ_CXX_STANDARD=20

cmake --build "${BUILD_DIR}"

RUN_CMD=(python3 "${ROOT_DIR}/benchwithzmq/run_comparison.py" "${PATTERN}" --runs "${RUNS}")

if [[ "${WITH_LIBZMQ}" -eq 1 ]]; then
  RUN_CMD+=(--refresh-libzmq)
else
  CACHE_FILE="${ROOT_DIR}/benchwithzmq/libzmq_cache.json"
  if [[ ! -f "${CACHE_FILE}" ]]; then
    echo "libzmq cache not found: ${CACHE_FILE}" >&2
    echo "Run with --with-libzmq once to generate the baseline." >&2
    exit 1
  fi
fi

if [[ -n "${OUTPUT_FILE}" ]]; then
  mkdir -p "$(dirname "${OUTPUT_FILE}")"
  "${RUN_CMD[@]}" | tee "${OUTPUT_FILE}"
else
  "${RUN_CMD[@]}"
fi
