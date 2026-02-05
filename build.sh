#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/core/build"
export TMPDIR=/tmp

echo "=== Clean Build ==="
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "=== CMake Configure ==="
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DWITH_DOCS=OFF \
  -DWITH_TLS=ON -DBUILD_BENCHMARKS=ON

echo "=== Build ==="
make -j$(nproc)

echo "=== Run Tests ==="
ctest --output-on-failure -j$(nproc)

echo "=== Done ==="
