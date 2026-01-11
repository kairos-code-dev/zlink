#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

ARCH="${1:-$(uname -m)}"
BUILD_TYPE="Release"

# Normalize architecture name
if [ "$ARCH" = "x86_64" ]; then
    ARCH="x64"
elif [ "$ARCH" = "aarch64" ]; then
    ARCH="arm64"
fi

if [ "$ARCH" != "x64" ] && [ "$ARCH" != "arm64" ]; then
    echo "Error: Invalid architecture '$ARCH'. Use 'x64' or 'arm64'"
    exit 1
fi

BUILD_DIR="$PROJECT_ROOT/build/linux-${ARCH}"
OUTPUT_DIR="$PROJECT_ROOT/dist/linux-${ARCH}"

echo ""
echo "==================================="
echo "Linux Build Configuration"
echo "==================================="
echo "Architecture:      ${ARCH}"
echo "Build type:        ${BUILD_TYPE}"
echo "Output directory:  ${OUTPUT_DIR}"
echo "==================================="
echo ""

# Clean and create directories
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
mkdir -p "$OUTPUT_DIR"

cd "$BUILD_DIR"

# Configure with CMake
echo "Configuring with CMake..."
cmake "$PROJECT_ROOT" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DBUILD_SHARED=ON \
    -DBUILD_STATIC=OFF \
    -DBUILD_TESTS=ON \
    -DWITH_DOCS=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON

# Build
echo "Building..."
make -j$(nproc)

# Copy artifacts
echo "Copying artifacts..."
SO_FILE=$(find lib -name "libzmq.so.5*" 2>/dev/null | head -n 1)
if [ -z "$SO_FILE" ]; then
    SO_FILE=$(find . -name "libzmq.so.5*" 2>/dev/null | head -n 1)
fi

if [ -n "$SO_FILE" ]; then
    cp "$SO_FILE" "$OUTPUT_DIR/libzmq.so"
    echo "Copied: $SO_FILE -> $OUTPUT_DIR/libzmq.so"
else
    echo "Error: libzmq.so not found!"
    exit 1
fi

# Copy headers
cp "$PROJECT_ROOT/include/zmq.h" "$OUTPUT_DIR/"
cp "$PROJECT_ROOT/include/zmq_utils.h" "$OUTPUT_DIR/"

echo ""
echo "==================================="
echo "Build completed successfully!"
echo "Output: $OUTPUT_DIR"
echo "==================================="
ls -la "$OUTPUT_DIR"
