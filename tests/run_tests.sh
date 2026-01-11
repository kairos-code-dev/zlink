#!/bin/bash
#
# run_tests.sh - Test runner for Linux/macOS
#
# This script builds and runs libzmq validation tests
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== libzmq Test Suite ===${NC}\n"

# Check if dist directory exists
if [ ! -d "${PROJECT_ROOT}/dist" ]; then
    echo -e "${RED}[ERROR] dist/ directory not found${NC}"
    echo "Please build libzmq first using:"
    if [[ "$OSTYPE" == "darwin"* ]]; then
        echo "  ./build-scripts/macos/build.sh"
    else
        echo "  ./build-scripts/linux/build.sh"
    fi
    exit 1
fi

# Detect platform
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS
    ARCH=$(uname -m)
    if [ "$ARCH" = "arm64" ]; then
        PLATFORM_DIR="macos-arm64"
    else
        PLATFORM_DIR="macos-x86_64"
    fi
else
    # Linux
    ARCH=$(uname -m)
    if [ "$ARCH" = "aarch64" ]; then
        PLATFORM_DIR="linux-arm64"
    else
        PLATFORM_DIR="linux-x64"
    fi
fi

DIST_PATH="${PROJECT_ROOT}/dist/${PLATFORM_DIR}"

echo -e "${BLUE}Platform:${NC} ${PLATFORM_DIR}"
echo -e "${BLUE}Library path:${NC} ${DIST_PATH}"
echo ""

# Check if libzmq exists
if [ ! -f "${DIST_PATH}/libzmq.so" ] && [ ! -f "${DIST_PATH}/libzmq.dylib" ]; then
    echo -e "${RED}[ERROR] libzmq not found in ${DIST_PATH}${NC}"
    echo "Please build libzmq first."
    exit 1
fi

# Create build directory
echo -e "${YELLOW}Preparing test build...${NC}"
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Configure with CMake
echo -e "${YELLOW}Configuring tests...${NC}"
if ! cmake .. -DCMAKE_BUILD_TYPE=Release; then
    echo -e "${RED}[ERROR] CMake configuration failed${NC}"
    exit 1
fi

# Build tests
echo -e "${YELLOW}Building tests...${NC}"
if ! cmake --build . --config Release; then
    echo -e "${RED}[ERROR] Test build failed${NC}"
    exit 1
fi

echo -e "${GREEN}[OK] Tests built successfully${NC}\n"

# Set library path for runtime
if [[ "$OSTYPE" == "darwin"* ]]; then
    export DYLD_LIBRARY_PATH="${DIST_PATH}:${DYLD_LIBRARY_PATH}"
else
    export LD_LIBRARY_PATH="${DIST_PATH}:${LD_LIBRARY_PATH}"
fi

# Run tests
echo -e "${BLUE}=== Running Tests ===${NC}\n"

TESTS_PASSED=0
TESTS_FAILED=0

# Test 1: Basic functionality
echo -e "${YELLOW}Running test_basic...${NC}"
if ./test_basic; then
    echo -e "${GREEN}[PASS] test_basic${NC}\n"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${RED}[FAIL] test_basic${NC}\n"
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi

# Test 2: CURVE encryption
echo -e "${YELLOW}Running test_curve...${NC}"
if ./test_curve; then
    echo -e "${GREEN}[PASS] test_curve${NC}\n"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${RED}[FAIL] test_curve${NC}\n"
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi

# Summary
echo -e "${BLUE}=== Test Summary ===${NC}"
echo -e "Tests passed: ${GREEN}${TESTS_PASSED}${NC}"
echo -e "Tests failed: ${RED}${TESTS_FAILED}${NC}"
echo ""

if [ ${TESTS_FAILED} -eq 0 ]; then
    echo -e "${GREEN}✓ All tests passed!${NC}"
    echo ""
    echo -e "${BLUE}libzmq validation successful:${NC}"
    echo "  ✓ Library loads correctly"
    echo "  ✓ Basic socket operations work"
    echo "  ✓ CURVE encryption is functional"
    echo "  ✓ libsodium is statically linked"
    exit 0
else
    echo -e "${RED}✗ Some tests failed${NC}"
    echo ""
    echo -e "${YELLOW}Troubleshooting:${NC}"
    echo "  1. Verify libzmq was built correctly"
    echo "  2. Check library dependencies:"
    if [[ "$OSTYPE" == "darwin"* ]]; then
        echo "     otool -L ${DIST_PATH}/libzmq.dylib"
    else
        echo "     ldd ${DIST_PATH}/libzmq.so"
    fi
    echo "  3. Ensure libsodium is statically linked (no external libsodium dependency)"
    exit 1
fi
