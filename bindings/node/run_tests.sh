#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

for test_file in dist-tools/tests/*.test.js; do
  printf '[test] %s\n' "$test_file"
  node --test "$test_file"
done
