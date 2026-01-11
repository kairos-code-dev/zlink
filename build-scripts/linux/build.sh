#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

ARCH="${1:-x64}"

case "$ARCH" in
    x64|x86_64)
        ARCH_NAME="x64"
        ;;
    arm64|aarch64)
        ARCH_NAME="arm64"
        ;;
    *)
        echo "Unknown architecture: $ARCH"
        exit 1
        ;;
esac

BUILD_DIR="$PROJECT_ROOT/build-$ARCH_NAME"
DIST_DIR="$PROJECT_ROOT/dist/linux-$ARCH_NAME"

echo "=== Building zlink for Linux $ARCH_NAME ==="

# Clean and create build directory
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
mkdir -p "$DIST_DIR"

cd "$BUILD_DIR"

# Configure
cmake "$PROJECT_ROOT" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=ON \
    -DWITH_DOCS=OFF \
    -DBUILD_SHARED=ON \
    -DBUILD_STATIC=ON

# Build
make -j$(nproc)

# Copy artifacts
cp -f lib/libzmq.so* "$DIST_DIR/" 2>/dev/null || true
cp -f lib/libzmq.a "$DIST_DIR/" 2>/dev/null || true
cp -f "$PROJECT_ROOT/include/zmq.h" "$DIST_DIR/"
cp -f "$PROJECT_ROOT/include/zmq_utils.h" "$DIST_DIR/"

echo "=== Build complete ==="
echo "Artifacts in: $DIST_DIR"
ls -la "$DIST_DIR"
