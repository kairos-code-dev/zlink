#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CODEC_PATHS=(
  "${ROOT_DIR}/codecs/zlink_codec_json/src"
  "${ROOT_DIR}/codecs/zlink_codec_messagepack/src"
  "${ROOT_DIR}/codecs/zlink_codec_protobuf/src"
)
PYTHONPATH_ENTRIES=("${ROOT_DIR}/src")
for codec_path in "${CODEC_PATHS[@]}"; do
  PYTHONPATH_ENTRIES+=("${codec_path}")
done
if [[ -n "${PYTHONPATH:-}" ]]; then
  PYTHONPATH_ENTRIES+=("${PYTHONPATH}")
fi
export PYTHONPATH="$(IFS=:; printf '%s' "${PYTHONPATH_ENTRIES[*]}")"
export PYTHONDONTWRITEBYTECODE="${PYTHONDONTWRITEBYTECODE:-1}"

cd "${ROOT_DIR}"
pytest -q "$@"
"${ROOT_DIR}/samples/run_samples.sh"
