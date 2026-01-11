#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Find the build directory
if [ -d "$PROJECT_ROOT/build-x64" ]; then
    BUILD_DIR="$PROJECT_ROOT/build-x64"
elif [ -d "$PROJECT_ROOT/build-arm64" ]; then
    BUILD_DIR="$PROJECT_ROOT/build-arm64"
else
    echo "Build directory not found"
    exit 1
fi

echo "=== Running tests ==="

cd "$BUILD_DIR"
ctest --output-on-failure -j$(nproc)

echo "=== Tests complete ==="
