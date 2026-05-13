#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CORE_LIB="${ROOT_DIR}/../core/build/lib/libzlink.so"
if [[ -f "${CORE_LIB}" ]]; then
  export ZLINK_LIBRARY_PATH="${CORE_LIB}"
  for native_dir in \
    "${ROOT_DIR}/src/main/resources/native/linux-x86_64" \
    "${ROOT_DIR}/build/resources/main/native/linux-x86_64"; do
    if [[ -d "${native_dir}" ]]; then
      cp -f "${CORE_LIB}" "${native_dir}/libzlink.so"
      cp -f "${ROOT_DIR}/../core/build/lib/libzlink.so.6.0.0" "${native_dir}/libzlink.so.6.0.0"
      ln -sfn libzlink.so.6.0.0 "${native_dir}/libzlink.so.6"
    fi
  done
fi
TASKS=(
  ":test"
  ":integrationTest"
  ":zlink-codec-json:test"
  ":zlink-codec-messagepack:test"
  ":zlink-codec-protobuf:test"
  ":zlink-ext-netty:test"
)

cd "${ROOT_DIR}"

failures=0
timeout_seconds=180

run_task() {
  if command -v timeout >/dev/null 2>&1; then
    timeout "${timeout_seconds}s" ./gradlew "$1" --no-daemon
    return $?
  fi
  ./gradlew "$1" --no-daemon
}

for task in "${TASKS[@]}"; do
  printf '[RUN] %s\n' "$task"
  if ! run_task "$task"; then
    printf '[FAIL] %s\n' "$task"
    failures=$((failures + 1))
  else
    printf '[PASS] %s\n' "$task"
  fi
done

printf '[RUN] samples/run_samples.sh\n'
if ! "${ROOT_DIR}/samples/run_samples.sh"; then
  printf '[FAIL] samples/run_samples.sh\n'
  failures=$((failures + 1))
else
  printf '[PASS] samples/run_samples.sh\n'
fi

if (( failures > 0 )); then
  printf 'Test summary: %d failed\n' "$failures"
  exit 1
fi

printf 'Test summary: all tasks and sample smoke passed\n'
