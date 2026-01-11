#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

ARCH="${1:-$(uname -m)}"
BUILD_TYPE="Release"

# Normalize architecture name
if [ "$ARCH" = "x64" ]; then
    ARCH="x86_64"
fi

if [ "$ARCH" != "x86_64" ] && [ "$ARCH" != "arm64" ]; then
    echo "Error: Invalid architecture '$ARCH'. Use 'x86_64' or 'arm64'"
    exit 1
fi

BUILD_DIR="$PROJECT_ROOT/build/macos-${ARCH}"
OUTPUT_DIR="$PROJECT_ROOT/dist/macos-${ARCH}"

echo ""
echo "==================================="
echo "macOS Build Configuration"
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
    -DCMAKE_OSX_ARCHITECTURES="$ARCH" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DBUILD_SHARED=ON \
    -DBUILD_STATIC=OFF \
    -DBUILD_TESTS=ON \
    -DWITH_DOCS=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_MACOSX_RPATH=ON

# Build
echo "Building..."
make -j$(sysctl -n hw.ncpu)

# Copy artifacts
echo "Copying artifacts..."
DYLIB_FILE=$(find lib -name "libzmq.5.dylib" 2>/dev/null | head -n 1)
if [ -z "$DYLIB_FILE" ]; then
    DYLIB_FILE=$(find . -name "libzmq.5.dylib" 2>/dev/null | head -n 1)
fi

if [ -n "$DYLIB_FILE" ]; then
    cp "$DYLIB_FILE" "$OUTPUT_DIR/libzmq.dylib"
    install_name_tool -id "@rpath/libzmq.dylib" "$OUTPUT_DIR/libzmq.dylib"
    echo "Copied: $DYLIB_FILE -> $OUTPUT_DIR/libzmq.dylib"
else
    echo "Error: libzmq.dylib not found!"
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
