#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

SUITE="single"
args=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --suite)
      SUITE="${2:-}"
      shift 2
      ;;
    *)
      args+=("$1")
      shift
      ;;
  esac
done

case "${SUITE}" in
  single)
    exec "${SCRIPT_DIR}/single/run_benchmarks.sh" "${args[@]}"
    ;;
  multi)
    exec "${SCRIPT_DIR}/multi/run_benchmarks.sh" "${args[@]}"
    ;;
  *)
    echo "unsupported suite: ${SUITE}" >&2
    exit 1
    ;;
esac
