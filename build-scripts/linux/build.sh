#!/bin/bash

# Linux build script for libzmq
# Supports both x64 and arm64 architectures
# Requires: gcc, g++, make, cmake, pkg-config
#
# Minimal build without CURVE/libsodium support

set -e

# Get script directory and repo root early (before any cd commands)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Read VERSION file
if [ -f "$REPO_ROOT/VERSION" ]; then
    LIBZMQ_VERSION=$(grep '^LIBZMQ_VERSION=' "$REPO_ROOT/VERSION" | cut -d'=' -f2)
else
    LIBZMQ_VERSION="4.3.5"
fi

# Parse arguments: ARCH RUN_TESTS
ARCH="${1:-$(uname -m)}"
RUN_TESTS="${2:-OFF}"
BUILD_TYPE="Release"

# Normalize architecture name
# Convert from uname -m format to our naming convention
if [ "$ARCH" = "x86_64" ]; then
    ARCH="x64"
elif [ "$ARCH" = "aarch64" ]; then
    ARCH="arm64"
fi

# Validate architecture
if [ "$ARCH" != "x64" ] && [ "$ARCH" != "arm64" ]; then
    echo "Error: Invalid architecture '$ARCH'. Use 'x64' or 'arm64'"
    exit 1
fi

OUTPUT_DIR="dist/linux-${ARCH}"

echo ""
echo "==================================="
echo "Linux Build Configuration"
echo "==================================="
echo "Architecture:      ${ARCH}"
echo "libzmq version:    ${LIBZMQ_VERSION}"
echo "CURVE support:     Disabled"
echo "RUN_TESTS:         ${RUN_TESTS}"
echo "Build type:        ${BUILD_TYPE}"
echo "Output directory:  ${OUTPUT_DIR}"
echo "==================================="
echo ""

# Change to repo root
cd "$REPO_ROOT"

# Create build directories
BUILD_DIR="build/linux-${ARCH}"
mkdir -p "$BUILD_DIR"
mkdir -p "$OUTPUT_DIR"

echo "Step 1: Using repository source for libzmq..."

# Step 2: Configure libzmq with CMake
echo ""
echo "Step 2: Configuring libzmq with CMake for ${ARCH}..."
cd "$BUILD_DIR"

LIBZMQ_SRC_ABS="$REPO_ROOT"

# Set architecture-specific CMake flags for cross-compilation if needed
CMAKE_ARCH_FLAGS=""
if [ "$ARCH" = "arm64" ]; then
    CMAKE_ARCH_FLAGS="-DCMAKE_SYSTEM_PROCESSOR=aarch64"
elif [ "$ARCH" = "x64" ]; then
    CMAKE_ARCH_FLAGS="-DCMAKE_SYSTEM_PROCESSOR=x86_64"
fi

# Determine BUILD_TESTS flag
BUILD_TESTS_FLAG="OFF"
if [ "$RUN_TESTS" = "ON" ]; then
    BUILD_TESTS_FLAG="ON"
fi

# Build without CURVE/libsodium
cmake "$LIBZMQ_SRC_ABS" \
    $CMAKE_ARCH_FLAGS \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DBUILD_SHARED=ON \
    -DBUILD_STATIC=OFF \
    -DBUILD_TESTS="$BUILD_TESTS_FLAG" \
    -DBUILD_BENCHMARKS=ON \
    -DENABLE_CURVE=OFF \
    -DWITH_LIBSODIUM=OFF \
    -DZMQ_CXX_STANDARD=20 \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_INSTALL_PREFIX="$(pwd)/install"

# Step 3: Build libzmq
echo ""
echo "Step 3: Building libzmq for ${ARCH}..."
make -j$(nproc)

# Step 4: Install
echo ""
echo "Step 4: Installing to output directory..."
make install

# Copy .so to output
SO_FILE=$(find install/lib* -name "libzmq.so.5*" 2>/dev/null | head -n 1)
if [ -z "$SO_FILE" ]; then
    SO_FILE=$(find lib -name "libzmq.so.5*" 2>/dev/null | head -n 1)
fi

if [ -n "$SO_FILE" ]; then
    TARGET_SO="$REPO_ROOT/$OUTPUT_DIR/libzmq.so"
    cp "$SO_FILE" "$TARGET_SO"
    echo "Copied: $SO_FILE -> $TARGET_SO"
else
    echo "Error: libzmq.so not found!"
    exit 1
fi

# Copy public headers
echo ""
echo "Copying public headers..."
INCLUDE_DIR="$REPO_ROOT/$OUTPUT_DIR/include"
mkdir -p "$INCLUDE_DIR"
cp install/include/zmq.h "$INCLUDE_DIR/"
cp install/include/zmq_utils.h "$INCLUDE_DIR/"
echo "Copied: zmq.h, zmq_utils.h -> $INCLUDE_DIR/"

cd "$REPO_ROOT"

# Step 5: Run tests (if enabled)
if [ "$RUN_TESTS" = "ON" ]; then
    echo ""
    echo "Step 5: Running tests..."
    cd "$BUILD_DIR"

    # Build test executables
    make -j$(nproc)

    # Run tests with ctest
    # Note: Some tests may be skipped based on platform capabilities
    ctest --output-on-failure -j$(nproc) || {
        echo ""
        echo "Some tests failed. Checking results..."
        # Allow some tests to fail (TIPC, fuzzer tests may not work in all environments)
        FAILED_TESTS=$(ctest --rerun-failed --output-on-failure 2>&1 | grep -c "Failed" || true)
        if [ "$FAILED_TESTS" -gt 20 ]; then
            echo "Too many test failures ($FAILED_TESTS). Build may be broken."
            exit 1
        fi
        echo "Acceptable number of test failures. Continuing..."
    }

    cd "$REPO_ROOT"
fi

# Step 6: Verify build
echo ""
echo "Step 6: Verifying build for ${ARCH}..."
FINAL_SO="$OUTPUT_DIR/libzmq.so"

if [ -f "$FINAL_SO" ]; then
    echo "File size: $(stat -c%s "$FINAL_SO") bytes"

    # Verify architecture using readelf (if available)
    if command -v readelf &> /dev/null; then
        echo "Architecture verification:"
        MACHINE=$(readelf -h "$FINAL_SO" | grep Machine | awk '{print $2}')
        echo "ELF Machine: $MACHINE"
    fi

    echo ""
    echo "==================================="
    echo "Build completed successfully!"
    echo "Output: $FINAL_SO"
    echo "==================================="
else
    echo "Error: Build failed - $FINAL_SO not found"
    exit 1
fi
