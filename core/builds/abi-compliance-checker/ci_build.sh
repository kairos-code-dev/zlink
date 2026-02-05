#!/usr/bin/env bash
#
# LEGACY SCRIPT: ABI/API Compliance Checker for upstream libzlink
#
# This script is NOT used for zlink builds. It's a legacy CI tool that:
# 1. Builds the current HEAD of upstream libzlink using autotools
# 2. Builds the latest release tag of upstream libzlink using autotools
# 3. Compares ABI/API compatibility between versions
#
# zlink uses CMake, but this script uses autotools because it's checking
# compatibility with upstream libzlink releases which use autotools.
#
# This script should only be used for upstream libzlink compatibility analysis.

set -x
set -e

cd ../../

mkdir tmp
BUILD_PREFIX=$PWD/tmp

CONFIG_OPTS=()
CONFIG_OPTS+=("CFLAGS=-I${BUILD_PREFIX}/include -g -Og")
CONFIG_OPTS+=("CPPFLAGS=-I${BUILD_PREFIX}/include")
CONFIG_OPTS+=("CXXFLAGS=-I${BUILD_PREFIX}/include -g -Og")
CONFIG_OPTS+=("LDFLAGS=-L${BUILD_PREFIX}/lib")
CONFIG_OPTS+=("PKG_CONFIG_PATH=${BUILD_PREFIX}/lib/pkgconfig")
CONFIG_OPTS+=("--prefix=${BUILD_PREFIX}")
CONFIG_OPTS+=("--enable-drafts=no")

function print_abi_api_breakages() {
   echo "ABI breakages detected:"
   cat compat_reports/libzlink/${LATEST_VERSION}_to_HEAD/abi_affected.txt | c++filt
   echo "API breakages detected:"
   cat compat_reports/libzlink/${LATEST_VERSION}_to_HEAD/src_affected.txt | c++filt
   exit 1
}

git fetch --unshallow
git fetch --all --tags
LATEST_VERSION=$(git describe --abbrev=0 --tags)

./autogen.sh
./configure "${CONFIG_OPTS[@]}"
make VERBOSE=1 -j5
abi-dumper src/.libs/libzlink.so -o ${BUILD_PREFIX}/libzlink.head.dump -lver HEAD

git clone --depth 1 -b ${LATEST_VERSION} https://github.com/zlink/libzlink.git latest_release
cd latest_release
./autogen.sh
./configure "${CONFIG_OPTS[@]}"
make VERBOSe=1 -j5
abi-dumper src/.libs/libzlink.so -o ${BUILD_PREFIX}/libzlink.latest.dump -lver ${LATEST_VERSION}

abi-compliance-checker -l libzlink -d1 ${BUILD_PREFIX}/libzlink.latest.dump -d2 ${BUILD_PREFIX}/libzlink.head.dump -list-affected || print_abi_api_breakages
