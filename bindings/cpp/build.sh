#!/usr/bin/env bash
set -euo pipefail

# Usage:
#   ./bindings/cpp/build.sh [RUN_TESTS] [BUILD_EXAMPLES]
# Example:
#   ./bindings/cpp/build.sh ON OFF

RUN_TESTS="${1:-ON}"
BUILD_EXAMPLES="${2:-OFF}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/bindings/cpp/build"

mkdir -p "${BUILD_DIR}"

if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
  cached_src="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "${BUILD_DIR}/CMakeCache.txt" || true)"
  if [[ -n "${cached_src}" && "${cached_src}" != "${ROOT_DIR}" ]]; then
    rm -rf "${BUILD_DIR}"
    mkdir -p "${BUILD_DIR}"
  fi
fi

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
  -DZLINK_BUILD_CPP_BINDINGS=ON \
  -DZLINK_CPP_BUILD_TESTS="${RUN_TESTS}" \
  -DZLINK_CPP_BUILD_EXAMPLES="${BUILD_EXAMPLES}"

cmake --build "${BUILD_DIR}" -j"$(nproc)"
