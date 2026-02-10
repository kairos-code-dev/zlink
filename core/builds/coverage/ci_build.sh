#!/usr/bin/env bash
#
# NOTE: zlink uses CMake build system (not autotools)
# This script builds with code coverage support using CMake.
# Preferred report toolchain: lcov + genhtml
# Fallback report toolchain: gcovr

set -x
set -e

mkdir -p tmp build_coverage
BUILD_PREFIX=$PWD/tmp
BUILD_DIR=$PWD/build_coverage
JOBS=${JOBS:-$(nproc)}

have_cmd() {
    command -v "$1" >/dev/null 2>&1
}

emit_tool_hint_and_fail() {
    echo "Coverage report tool not found."
    echo "Install one of the following toolchains:"
    echo "  1) lcov + genhtml"
    echo "  2) gcovr"
    exit 2
}

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
    cmake --build . --verbose -j"${JOBS}" &&
    ctest --output-on-failure &&
    if have_cmd lcov && have_cmd genhtml; then
        # Generate coverage report with lcov.
        lcov --capture --directory . --output-file lcov.info &&
        lcov --remove lcov.info '/usr/*' '*/tests/*' '*/external/*' --output-file lcov.info &&
        genhtml lcov.info --output-directory coverage
    elif have_cmd gcovr; then
        # Fallback: generate both summary and HTML report with gcovr.
        gcovr --root ../.. --filter '../..\/src' --exclude '../..\/tests' --exclude '../..\/external' --print-summary &&
        gcovr --root ../.. --filter '../..\/src' --exclude '../..\/tests' --exclude '../..\/external' --html-details -o coverage/index.html
    else
        emit_tool_hint_and_fail
    fi
) || exit 1

echo "Coverage report generated in ${BUILD_DIR}/coverage/"
