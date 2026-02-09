#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
export TMPDIR=/tmp

echo "=== Clean Build ==="
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
echo "=== CMake Configure ==="
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTS=ON -DWITH_DOCS=OFF -DWITH_TLS=ON -DBUILD_BENCHMARKS=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

if [ -f "${BUILD_DIR}/compile_commands.json" ]; then
  ln -sfn "${BUILD_DIR}/compile_commands.json" "${ROOT_DIR}/compile_commands.json"
  echo "Linked compile_commands.json -> ${BUILD_DIR}/compile_commands.json"
fi

echo "=== Build ==="
cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo "=== Run Tests ==="
ctest --output-on-failure -j"$(nproc)" --test-dir "${BUILD_DIR}"

echo "=== Done ==="
