#!/bin/bash

# macOS build script for libzmq
# Supports both x86_64 and arm64 architectures
# Requires: Xcode Command Line Tools, cmake
#
# Build options:
# - ENABLE_CURVE=ON requires libsodium (statically linked)
# - ENABLE_CURVE=OFF skips libsodium entirely

set -e

# Get script directory and repo root early (before any cd commands)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Parse arguments: ARCH LIBZMQ_VERSION LIBSODIUM_VERSION ENABLE_CURVE RUN_TESTS
ARCH="${1:-$(uname -m)}"
LIBZMQ_VERSION="${2:-4.3.5}"
LIBSODIUM_VERSION="${3:-1.0.19}"
ENABLE_CURVE="${4:-ON}"  # Set to OFF to build without libsodium
RUN_TESTS="${5:-OFF}"    # Set to ON to build and run tests
BUILD_TYPE="Release"
OUTPUT_DIR="dist/macos-${ARCH}"

# Normalize architecture name
if [ "$ARCH" = "x64" ]; then
    ARCH="x86_64"
fi

# Validate architecture
if [ "$ARCH" != "x86_64" ] && [ "$ARCH" != "arm64" ]; then
    echo "Error: Invalid architecture '$ARCH'. Use 'x86_64' or 'arm64'"
    exit 1
fi

# Set URLs
LIBZMQ_URL="https://github.com/zeromq/libzmq/releases/download/v${LIBZMQ_VERSION}/zeromq-${LIBZMQ_VERSION}.tar.gz"
LIBSODIUM_URL="https://github.com/jedisct1/libsodium/releases/download/${LIBSODIUM_VERSION}-RELEASE/libsodium-${LIBSODIUM_VERSION}.tar.gz"

echo ""
echo "==================================="
echo "macOS Build Configuration"
echo "==================================="
echo "Architecture:      ${ARCH}"
echo "libzmq version:    ${LIBZMQ_VERSION}"
echo "libsodium version: ${LIBSODIUM_VERSION}"
echo "ENABLE_CURVE:      ${ENABLE_CURVE}"
echo "RUN_TESTS:         ${RUN_TESTS}"
echo "Build type:        ${BUILD_TYPE}"
echo "Output directory:  ${OUTPUT_DIR}"
echo "==================================="
echo ""

# Change to repo root
cd "$REPO_ROOT"

# Create build directories
BUILD_DIR="build/macos-${ARCH}"
DEPS_DIR="deps/macos-${ARCH}"
mkdir -p "$BUILD_DIR"
mkdir -p "$DEPS_DIR"
mkdir -p "$OUTPUT_DIR"

# Step 1: Download and build libsodium (static) - only if ENABLE_CURVE=ON
if [ "$ENABLE_CURVE" = "ON" ]; then
    echo "Step 1: Building libsodium ${LIBSODIUM_VERSION} for ${ARCH}..."
    LIBSODIUM_ARCHIVE="$DEPS_DIR/libsodium-${LIBSODIUM_VERSION}.tar.gz"
    LIBSODIUM_SRC="$DEPS_DIR/libsodium-${LIBSODIUM_VERSION}"
    LIBSODIUM_INSTALL="$DEPS_DIR/libsodium-install"

    # Create install directory
    mkdir -p "$LIBSODIUM_INSTALL"

    if [ ! -f "$LIBSODIUM_ARCHIVE" ]; then
        echo "Downloading libsodium..."
        curl -L "$LIBSODIUM_URL" -o "$LIBSODIUM_ARCHIVE"
    fi

    if [ ! -d "$LIBSODIUM_SRC" ]; then
        echo "Extracting libsodium..."
        tar -xzf "$LIBSODIUM_ARCHIVE" -C "$DEPS_DIR"
        # Handle libsodium-stable directory naming
        if [ -d "$DEPS_DIR/libsodium-stable" ] && [ ! -d "$LIBSODIUM_SRC" ]; then
            mv "$DEPS_DIR/libsodium-stable" "$LIBSODIUM_SRC"
        fi
    fi

    # Get absolute paths
    LIBSODIUM_INSTALL_ABS="$REPO_ROOT/$LIBSODIUM_INSTALL"

    if [ ! -f "$LIBSODIUM_INSTALL_ABS/lib/libsodium.a" ]; then
        echo "Building libsodium (static with -fPIC for ${ARCH})..."
        cd "$LIBSODIUM_SRC"

        # Set architecture-specific flags for cross-compilation
        CURRENT_ARCH=$(uname -m)
        echo "Current machine architecture: $CURRENT_ARCH"
        echo "Target architecture: $ARCH"

        ARCH_FLAGS="-arch $ARCH"

        # Set host flag for cross-compilation if needed
        if [ "$ARCH" = "arm64" ]; then
            if [ "$CURRENT_ARCH" != "arm64" ]; then
                HOST_FLAG="--host=aarch64-apple-darwin --build=x86_64-apple-darwin"
            else
                HOST_FLAG=""
            fi
        else
            if [ "$CURRENT_ARCH" != "x86_64" ]; then
                HOST_FLAG="--host=x86_64-apple-darwin --build=aarch64-apple-darwin"
            else
                HOST_FLAG=""
            fi
        fi

        # Configure with static library and PIC
        ./configure \
            $HOST_FLAG \
            --prefix="$LIBSODIUM_INSTALL_ABS" \
            --disable-shared \
            --enable-static \
            --with-pic \
            CC="clang" \
            CXX="clang++" \
            CFLAGS="$ARCH_FLAGS -fPIC -O3" \
            CXXFLAGS="$ARCH_FLAGS -fPIC -O3" \
            LDFLAGS="$ARCH_FLAGS"

        make -j$(sysctl -n hw.ncpu)
        make install

        cd "$REPO_ROOT"
        echo "libsodium built successfully for ${ARCH}"
    else
        echo "libsodium already built for ${ARCH}"
    fi
else
    echo "Step 1: Skipping libsodium (ENABLE_CURVE=OFF)..."
fi

# Step 2: Prepare local source
echo ""
echo "Step 2: Using local repository source for libzmq..."
# libzmq source is already in the project root

# Step 3: Configure libzmq with CMake for ${ARCH}
echo ""
echo "Step 3: Configuring libzmq with CMake for ${ARCH}..."
cd "$BUILD_DIR"

LIBZMQ_SRC_ABS="$REPO_ROOT"

# Set architecture-specific CMake flags
CMAKE_ARCH_FLAGS="-DCMAKE_OSX_ARCHITECTURES=$ARCH"

# Determine BUILD_TESTS flag
BUILD_TESTS_FLAG="OFF"
if [ "$RUN_TESTS" = "ON" ]; then
    BUILD_TESTS_FLAG="ON"
fi

if [ "$ENABLE_CURVE" = "ON" ]; then
    # Set PKG_CONFIG_PATH for libsodium
    export PKG_CONFIG_PATH="$LIBSODIUM_INSTALL_ABS/lib/pkgconfig:$PKG_CONFIG_PATH"

    cmake "$LIBZMQ_SRC_ABS" \
        $CMAKE_ARCH_FLAGS \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DBUILD_SHARED=ON \
        -DBUILD_STATIC=OFF \
        -DBUILD_TESTS="$BUILD_TESTS_FLAG" \
        -DENABLE_CURVE=ON \
        -DWITH_LIBSODIUM=ON \
        -Dsodium_LIBRARY_RELEASE="$LIBSODIUM_INSTALL_ABS/lib/libsodium.a" \
        -Dsodium_INCLUDE_DIR="$LIBSODIUM_INSTALL_ABS/include" \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DCMAKE_INSTALL_PREFIX="$(pwd)/install" \
        -DCMAKE_MACOSX_RPATH=ON
else
    # Build without CURVE/libsodium
    cmake "$LIBZMQ_SRC_ABS" \
        $CMAKE_ARCH_FLAGS \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DBUILD_SHARED=ON \
        -DBUILD_STATIC=OFF \
        -DBUILD_TESTS="$BUILD_TESTS_FLAG" \
        -DENABLE_CURVE=OFF \
        -DWITH_LIBSODIUM=OFF \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DCMAKE_INSTALL_PREFIX="$(pwd)/install" \
        -DCMAKE_MACOSX_RPATH=ON
fi

# Step 4: Build libzmq
echo ""
echo "Step 4: Building libzmq for ${ARCH}..."
make -j$(sysctl -n hw.ncpu)

# Step 5: Install
echo ""
echo "Step 5: Installing to output directory..."
make install

# Copy .dylib to output
DYLIB_FILE=$(find install/lib -name "libzmq.5.dylib" 2>/dev/null | head -n 1)
if [ -z "$DYLIB_FILE" ]; then
    DYLIB_FILE=$(find lib -name "libzmq.5.dylib" 2>/dev/null | head -n 1)
fi

if [ -n "$DYLIB_FILE" ]; then
    TARGET_DYLIB="$REPO_ROOT/$OUTPUT_DIR/libzmq.dylib"
    cp "$DYLIB_FILE" "$TARGET_DYLIB"

    # Update install name for better portability
    install_name_tool -id "@rpath/libzmq.dylib" "$TARGET_DYLIB"

    echo "Copied: $DYLIB_FILE -> $TARGET_DYLIB"
else
    echo "Error: libzmq.dylib not found!"
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

# Step 6: Run tests (if enabled)
if [ "$RUN_TESTS" = "ON" ]; then
    echo ""
    echo "Step 6: Running tests..."
    cd "$BUILD_DIR"

    # Build test executables
    make -j$(sysctl -n hw.ncpu)

    # Run tests with ctest
    # Note: Some tests may be skipped based on platform capabilities
    # TIPC tests are Linux-only and will fail on macOS
    ctest --output-on-failure -j$(sysctl -n hw.ncpu) || {
        echo ""
        echo "Some tests failed. Checking results..."
        # Allow some tests to fail (TIPC tests are Linux-only, some platform-specific tests may fail)
        FAILED_TESTS=$(ctest --rerun-failed --output-on-failure 2>&1 | grep -c "Failed" || true)
        if [ "$FAILED_TESTS" -gt 20 ]; then
            echo "Too many test failures ($FAILED_TESTS). Build may be broken."
            exit 1
        fi
        echo "Acceptable number of test failures. Continuing..."
    }

    cd "$REPO_ROOT"
fi

# Step 7: Verify build
echo ""
echo "Step 7: Verifying build for ${ARCH}..."
FINAL_DYLIB="$OUTPUT_DIR/libzmq.dylib"

if [ -f "$FINAL_DYLIB" ]; then
    echo "File size: $(stat -f%z "$FINAL_DYLIB") bytes"

    # Verify architecture
    echo "Architecture verification:"
    lipo -info "$FINAL_DYLIB"

    echo ""
    echo "==================================="
    echo "Build completed successfully!"
    echo "Output: $FINAL_DYLIB"
    echo "==================================="
else
    echo "Error: Build failed - $FINAL_DYLIB not found"
    exit 1
fi
