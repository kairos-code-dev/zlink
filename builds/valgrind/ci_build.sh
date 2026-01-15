#!/usr/bin/env bash
#
# NOTE: zlink uses CMake build system (not autotools)
# This script builds with valgrind memory checking support

set -x
set -e

mkdir -p tmp build_valgrind
BUILD_PREFIX=$PWD/tmp

# Build libsodium dependency if CURVE is enabled
if [ -z $CURVE ]; then
    CURVE_OPTS="-DENABLE_CURVE=OFF"
elif [ $CURVE == "libsodium" ]; then
    CURVE_OPTS="-DWITH_LIBSODIUM=ON"

    if ! ((command -v dpkg-query >/dev/null 2>&1 && dpkg-query --list libsodium-dev >/dev/null 2>&1) || \
            (command -v brew >/dev/null 2>&1 && brew ls --versions libsodium >/dev/null 2>&1)); then
        # NOTE: libsodium (external dependency) uses autotools - this is intentional
        git clone --depth 1 -b stable https://github.com/jedisct1/libsodium.git
        ( cd libsodium; ./autogen.sh; ./configure --prefix=$BUILD_PREFIX; make install)
    fi
fi

# Build zlink with debug symbols for valgrind
CMAKE_OPTS=()
CMAKE_OPTS+=("-DCMAKE_BUILD_TYPE=Debug")
CMAKE_OPTS+=("-DCMAKE_INSTALL_PREFIX=${BUILD_PREFIX}")
CMAKE_OPTS+=("-DCMAKE_PREFIX_PATH=${BUILD_PREFIX}")
CMAKE_OPTS+=("-DBUILD_TESTS=ON")
CMAKE_OPTS+=("${CURVE_OPTS}")

if [ -n "$TLS" ] && [ "$TLS" == "enabled" ]; then
    CMAKE_OPTS+=("-DWITH_TLS=ON")
fi

# Build, check, and install from local source
(
    cd build_valgrind &&
    cmake ../.. "${CMAKE_OPTS[@]}" &&
    cmake --build . -j5 &&
    # Run tests under valgrind
    ctest --verbose --timeout 300 -T memcheck --output-on-failure
) || exit 1
