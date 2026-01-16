#!/usr/bin/env bash

set -x
set -e
export TMPDIR=/tmp

if [ $BUILD_TYPE = "default" ]; then
    mkdir -p build tmp
    BUILD_PREFIX=$PWD/tmp
    export TMPDIR=$PWD/tmp

    # zlink uses CMake build system (not autotools)
    # Build and check this project
    (
        cd build &&
        cmake .. \
            -DCMAKE_INSTALL_PREFIX="${BUILD_PREFIX}" \
            -DBUILD_TESTS=ON \
            -DBUILD_STATIC=OFF &&
        cmake --build . --verbose -j5 &&
        ctest --output-on-failure
    ) || exit 1
else
    cd ./builds/${BUILD_TYPE} && ./ci_build.sh
fi
