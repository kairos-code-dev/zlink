#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
ROOT_DIR="$(cd "${CPP_DIR}/../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/core/build"

CONFIGURE_ARGS=(
  -DZLINK_BUILD_CPP_BINDINGS=ON
  -DZLINK_CPP_BUILD_TESTS=ON
)

if [[ $# -gt 0 ]]; then
  CONFIGURE_ARGS+=("$@")
fi

echo "[cpp-tests] configure: ${BUILD_DIR}"
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" "${CONFIGURE_ARGS[@]}"

echo "[cpp-tests] build"
cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo "[cpp-tests] run contract tests"
if ctest --test-dir "${BUILD_DIR}" --output-on-failure -L contract; then
  echo "[cpp-tests] PASS"
else
  echo "[cpp-tests] FAIL"
  exit 1
fi
