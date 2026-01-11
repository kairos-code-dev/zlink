#!/bin/bash

# Linux build script for libzmq
# Supports both x64 and arm64 architectures
# Requires: gcc, g++, make, cmake, pkg-config, autoconf, automake, libtool
#
# Build options:
# - ENABLE_CURVE=ON requires libsodium (statically linked)
# - ENABLE_CURVE=OFF skips libsodium entirely

set -e

# Get script directory and repo root early (before any cd commands)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Parse arguments: ARCH LIBZMQ_VERSION LIBSODIUM_VERSION ENABLE_CURVE
ARCH="${1:-$(uname -m)}"
LIBZMQ_VERSION="${2:-4.3.5}"
LIBSODIUM_VERSION="${3:-1.0.19}"
ENABLE_CURVE="${4:-ON}"  # Set to OFF to build without libsodium
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

# Set URLs
LIBZMQ_URL="https://github.com/zeromq/libzmq/releases/download/v${LIBZMQ_VERSION}/zeromq-${LIBZMQ_VERSION}.tar.gz"
LIBSODIUM_URL="https://github.com/jedisct1/libsodium/releases/download/${LIBSODIUM_VERSION}-RELEASE/libsodium-${LIBSODIUM_VERSION}.tar.gz"

echo ""
echo "==================================="
echo "Linux Build Configuration"
echo "==================================="
echo "Architecture:      ${ARCH}"
echo "libzmq version:    ${LIBZMQ_VERSION}"
echo "libsodium version: ${LIBSODIUM_VERSION}"
echo "ENABLE_CURVE:      ${ENABLE_CURVE}"
echo "Build type:        ${BUILD_TYPE}"
echo "Output directory:  ${OUTPUT_DIR}"
echo "==================================="
echo ""

# Change to repo root
cd "$REPO_ROOT"

# Create build directories
BUILD_DIR="build/linux-${ARCH}"
DEPS_DIR="deps/linux-${ARCH}"
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
        # Normalize current architecture
        if [ "$CURRENT_ARCH" = "x86_64" ]; then
            CURRENT_ARCH="x64"
        elif [ "$CURRENT_ARCH" = "aarch64" ]; then
            CURRENT_ARCH="arm64"
        fi

        echo "Current machine architecture: $CURRENT_ARCH"
        echo "Target architecture: $ARCH"

        # Set host flag for cross-compilation if needed
        HOST_FLAG=""
        if [ "$ARCH" = "arm64" ] && [ "$CURRENT_ARCH" != "arm64" ]; then
            HOST_FLAG="--host=aarch64-linux-gnu"
        elif [ "$ARCH" = "x64" ] && [ "$CURRENT_ARCH" != "x64" ]; then
            HOST_FLAG="--host=x86_64-linux-gnu"
        fi

        # Configure with static library and PIC
        ./configure \
            $HOST_FLAG \
            --prefix="$LIBSODIUM_INSTALL_ABS" \
            --disable-shared \
            --enable-static \
            --with-pic \
            CFLAGS="-fPIC -O3" \
            CXXFLAGS="-fPIC -O3"

        make -j$(nproc)
        make install

        cd "$REPO_ROOT"
        echo "libsodium built successfully for ${ARCH}"
    else
        echo "libsodium already built for ${ARCH}"
    fi
else
    echo "Step 1: Skipping libsodium (ENABLE_CURVE=OFF)..."
fi

# Step 2: Download and extract libzmq
echo ""
echo "Step 2: Downloading libzmq ${LIBZMQ_VERSION}..."
LIBZMQ_ARCHIVE="$DEPS_DIR/zeromq-${LIBZMQ_VERSION}.tar.gz"
LIBZMQ_SRC="$DEPS_DIR/zeromq-${LIBZMQ_VERSION}"

if [ ! -f "$LIBZMQ_ARCHIVE" ]; then
    curl -L "$LIBZMQ_URL" -o "$LIBZMQ_ARCHIVE"
fi

if [ ! -d "$LIBZMQ_SRC" ]; then
    echo "Extracting libzmq..."
    tar -xzf "$LIBZMQ_ARCHIVE" -C "$DEPS_DIR"
fi

# Step 3: Configure libzmq with CMake
echo ""
echo "Step 3: Configuring libzmq with CMake for ${ARCH}..."
cd "$BUILD_DIR"

LIBZMQ_SRC_ABS="$REPO_ROOT/$LIBZMQ_SRC"

# Set architecture-specific CMake flags for cross-compilation if needed
CMAKE_ARCH_FLAGS=""
if [ "$ARCH" = "arm64" ]; then
    CMAKE_ARCH_FLAGS="-DCMAKE_SYSTEM_PROCESSOR=aarch64"
elif [ "$ARCH" = "x64" ]; then
    CMAKE_ARCH_FLAGS="-DCMAKE_SYSTEM_PROCESSOR=x86_64"
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
        -DBUILD_TESTS=OFF \
        -DENABLE_CURVE=ON \
        -DWITH_LIBSODIUM=ON \
        -Dsodium_LIBRARY_RELEASE="$LIBSODIUM_INSTALL_ABS/lib/libsodium.a" \
        -Dsodium_INCLUDE_DIR="$LIBSODIUM_INSTALL_ABS/include" \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DCMAKE_INSTALL_PREFIX="$(pwd)/install"
else
    # Build without CURVE/libsodium
    cmake "$LIBZMQ_SRC_ABS" \
        $CMAKE_ARCH_FLAGS \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DBUILD_SHARED=ON \
        -DBUILD_STATIC=OFF \
        -DBUILD_TESTS=OFF \
        -DENABLE_CURVE=OFF \
        -DWITH_LIBSODIUM=OFF \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DCMAKE_INSTALL_PREFIX="$(pwd)/install"
fi

# Step 4: Build libzmq
echo ""
echo "Step 4: Building libzmq for ${ARCH}..."
make -j$(nproc)

# Step 5: Install
echo ""
echo "Step 5: Installing to output directory..."
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
