#!/usr/bin/env bash
#
# Android NDK build script for zlink and dependencies
#
# This script uses autotools for building dependencies
# through the android_build_helper.sh utility functions.
#
# The android_build_helper.sh provides generic autotools support for
# cross-compiling dependencies to Android targets.
#
# zlink itself is built via CMake in the android_build_library() function,
# but the helper script checks for autotools first for dependency compatibility.

#   Exit if any step fails
set -e

# Use directory of current script as the working directory
cd "$( dirname "${BASH_SOURCE[0]}" )"
PROJECT_ROOT="$(cd ../.. && pwd)"

########################################################################
# Configuration & tuning options.
########################################################################
# Set default values used in ci builds
export NDK_VERSION="${NDK_VERSION:-android-ndk-r25}"

# Set default path to find Android NDK.
# Must be of the form <path>/${NDK_VERSION} !!
export ANDROID_NDK_ROOT="${ANDROID_NDK_ROOT:-/tmp/${NDK_VERSION}}"

# With NDK r22b, the minimum SDK version range is [16, 31].
# Since NDK r24, the minimum SDK version range is [19, 31].
# SDK version 21 is the minimum version for 64-bit builds.
export MIN_SDK_VERSION=${MIN_SDK_VERSION:-21}

# Use directory of current script as the build directory
# ${ANDROID_BUILD_DIR}/prefix/<build_arch>/lib will contain produced libraries
export ANDROID_BUILD_DIR="${ANDROID_BUILD_DIR:-${PWD}}"

# Where to download our dependencies: default to /tmp/tmp-deps
export ANDROID_DEPENDENCIES_DIR="${ANDROID_DEPENDENCIES_DIR:-/tmp/tmp-deps}"

# Clean before processing
export ANDROID_BUILD_CLEAN="${ANDROID_BUILD_CLEAN:-no}"

# Set this to 'no', to enable verbose ./configure
export CI_CONFIG_QUIET="${CI_CONFIG_QUIET:-no}"

# By default, dependencies will be cloned to /tmp/tmp-deps.

########################################################################
# Utilities
########################################################################
# Get access to android_build functions and variables
# Perform some sanity checks and calculate some variables.
source "${PROJECT_ROOT}/builds/android/android_build_helper.sh"

function usage {
    echo "LIBZMQ - Usage:"
    echo "  export XXX=xxx"
    echo "  ./build.sh [ arm | arm64 | x86 | x86_64 ]"
    echo ""
    echo "See this file (configuration & tuning options) for details"
    echo "on variables XXX and their values xxx"
    exit 1
}

########################################################################
# Sanity checks
########################################################################
BUILD_ARCH="$1"
[ -z "${BUILD_ARCH}" ] && usage

########################################################################
# Compilation
########################################################################
# Choose a C++ standard library implementation from the ndk
export ANDROID_BUILD_CXXSTL="gnustl_shared_49"

# Additional flags for LIBTOOL, for LIBZMQ and other dependencies.
export LIBTOOL_EXTRA_LDFLAGS='-avoid-version'

# Set up android build environment and set ANDROID_BUILD_OPTS array
android_build_set_env "${BUILD_ARCH}"
android_download_ndk
android_build_env
android_build_opts

# Check for environment variable to clear the prefix and do a clean build
if [ "${ANDROID_BUILD_CLEAN}" = "yes" ]; then
    android_build_trace "Doing a clean build (removing previous build and dependencies)..."
    rm -rf "${ANDROID_BUILD_PREFIX:?}"/*

    # Called shells MUST not clean after ourselves !
    export ANDROID_BUILD_CLEAN="no"
fi

DEPENDENCIES=()

##
# Build libzmq from local source

(android_build_verify_so "libzmq.so" "${DEPENDENCIES[@]}" &> /dev/null) || {
    (
        CONFIG_OPTS=()
        [ "${CI_CONFIG_QUIET}" = "yes" ] && CONFIG_OPTS+=("--quiet")
        CONFIG_OPTS+=("${ANDROID_BUILD_OPTS[@]}")
        CONFIG_OPTS+=("--without-docs")

        android_build_library "LIBZMQ" "${PROJECT_ROOT}"
    ) || exit 1
}

##
# Fetch the STL as well.

cp "${ANDROID_STL_ROOT}/${ANDROID_STL}" "${ANDROID_BUILD_PREFIX}/lib/."

##
# Verify shared libraries in prefix
for library in "libzmq.so" "${DEPENDENCIES[@]}" ; do
    android_build_verify_so "${library}"
done

android_build_verify_so "libzmq.so" "${DEPENDENCIES[@]}" "${ANDROID_STL}"
android_build_trace "Android build successful"
