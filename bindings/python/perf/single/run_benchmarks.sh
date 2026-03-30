#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHONPATH_DIR="$(cd "$ROOT_DIR/../../src" && pwd)"

patterns="PAIR,PUBSUB,DEALER_ROUTER,SPOT"
duration="2"
warmup="1"
msg_size="256"
recv="callback"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --pattern)
      patterns="$2"
      shift 2
      ;;
    --duration)
      duration="$2"
      shift 2
      ;;
    --warmup)
      warmup="$2"
      shift 2
      ;;
    --msg-size)
      msg_size="$2"
      shift 2
      ;;
    --recv)
      recv="$2"
      shift 2
      ;;
    --help)
      cat <<'EOF'
usage: run_benchmarks.sh [--pattern LIST] [--duration N] [--warmup N] [--msg-size N] [--recv callback]
EOF
      exit 0
      ;;
    *)
      echo "unsupported argument: $1" >&2
      exit 1
      ;;
  esac
done

IFS=',' read -r -a pattern_list <<< "$patterns"

for pattern in "${pattern_list[@]}"; do
  case "$pattern" in
    ALL)
      for nested in PAIR PUBSUB DEALER_ROUTER SPOT; do
        PYTHONPATH="$PYTHONPATH_DIR" python "$ROOT_DIR/perf_${nested,,}.py" \
          --duration "$duration" --warmup "$warmup" --msg-size "$msg_size" --recv "$recv"
      done
      ;;
    PAIR|PUBSUB|DEALER_ROUTER|SPOT)
      PYTHONPATH="$PYTHONPATH_DIR" python "$ROOT_DIR/perf_${pattern,,}.py" \
        --duration "$duration" --warmup "$warmup" --msg-size "$msg_size" --recv "$recv"
      ;;
    *)
      echo "unsupported pattern: $pattern" >&2
      exit 1
      ;;
  esac
done
