#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/core/build"
REPEAT_COUNT=1000

TEST_NAMES=(
  test_service_introspection_discovery_control_path
  test_service_introspection_discovery_ordering
  test_service_introspection_discovery_self_close
  test_service_introspection_discovery_lifecycle
  test_service_introspection_registry_lifecycle
  test_service_introspection_registry_query_lifecycle
  test_gateway_runtime_reads
  test_gateway_send_plane_updates
  test_gateway_attach_query_ordering
  test_gateway_send_ready_self_close
  test_gateway_monitor_self_close
  test_spot_service_introspection_monitors
  test_spot_service_introspection_late_connect
  test_spot_service_introspection_subscription_ready_loss
  test_spot_service_introspection_handler_monitor_close
  test_spot_service_introspection_snapshots
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

echo "=== Verifying stress test registration ==="
AVAILABLE_TESTS="$(ctest --test-dir "${BUILD_DIR}" -N)"
for test_name in "${TEST_NAMES[@]}"; do
  if ! printf '%s\n' "${AVAILABLE_TESTS}" | grep -Eq "Test +#[0-9]+: ${test_name}$"; then
    echo "Configured stress test is not registered in CTest: ${test_name}" >&2
    exit 1
  fi
done
echo "=== Stress test registration verified ==="

for test_name in "${TEST_NAMES[@]}"; do
  echo "=== Repeating ${test_name} (${REPEAT_COUNT} iterations) ==="
  iteration=1
  while [[ "${iteration}" -le "${REPEAT_COUNT}" ]]; do
    echo "--- ${test_name} iteration ${iteration}/${REPEAT_COUNT} ---"
    ctest --test-dir "${BUILD_DIR}" \
      --output-on-failure \
      -R "^${test_name}$"
    iteration=$((iteration + 1))
  done
done

echo "=== Thread-safe contract stress completed ==="
