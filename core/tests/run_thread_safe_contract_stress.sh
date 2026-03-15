#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/core/build"
REPEAT_COUNT=1000

TEST_NAMES=(
  test_gateway_runtime_reads
  test_spot_service_introspection_runtime_reads
  test_gateway_lifecycle_contract
  test_spot_service_introspection_lifecycle_contract
  test_spot_service_introspection_handle_lifecycle
)

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Run thread-safe contract regression cases repeatedly via CTest.

Options:
  --build-dir PATH   Build directory containing CTest metadata
                     (default: ${BUILD_DIR})
  --count N          Repeat count for each selected test
                     (default: ${REPEAT_COUNT})
  -h, --help         Show this help text

Examples:
  $(basename "$0")
  $(basename "$0") --count 100
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      BUILD_DIR="$2"
      shift 2
      ;;
    --count)
      REPEAT_COUNT="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ ! -f "${BUILD_DIR}/CTestTestfile.cmake" ]]; then
  echo "Build directory does not look configured for CTest: ${BUILD_DIR}" >&2
  exit 1
fi

for test_name in "${TEST_NAMES[@]}"; do
  echo "=== Repeating ${test_name} (${REPEAT_COUNT} iterations) ==="
  ctest --test-dir "${BUILD_DIR}" \
    --output-on-failure \
    --repeat "until-fail:${REPEAT_COUNT}" \
    -R "^${test_name}$"
done

echo "=== Thread-safe contract stress completed ==="
