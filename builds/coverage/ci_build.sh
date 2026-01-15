#!/usr/bin/env bash
#
# NOTE: zlink uses CMake build system (not autotools)
# This script builds with code coverage support using CMake and gcov/lcov

set -x
set -e

mkdir -p tmp build_coverage
BUILD_PREFIX=$PWD/tmp

# Build with code coverage enabled
# Requires: gcov, lcov, genhtml
(
    cd build_coverage &&
    cmake ../.. \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_INSTALL_PREFIX="${BUILD_PREFIX}" \
        -DCMAKE_C_FLAGS="--coverage -fprofile-arcs -ftest-coverage" \
        -DCMAKE_CXX_FLAGS="--coverage -fprofile-arcs -ftest-coverage" \
        -DCMAKE_EXE_LINKER_FLAGS="--coverage" \
        -DBUILD_TESTS=ON &&
    cmake --build . --verbose -j5 &&
    ctest --output-on-failure &&
    # Generate coverage report
    lcov --capture --directory . --output-file lcov.info &&
    lcov --remove lcov.info '/usr/*' '*/tests/*' '*/external/*' --output-file lcov.info &&
    genhtml lcov.info --output-directory coverage
) || exit 1

echo "Coverage report generated in build_coverage/coverage/"
