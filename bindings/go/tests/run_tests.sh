#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

cd "${ROOT_DIR}"

echo "==> go test binding packages"
go test $(go list ./... | grep -v '/perf')

for codec in codec/json codec/messagepack codec/proto; do
  echo "==> go test ./${codec}"
  (cd "${ROOT_DIR}/${codec}" && go test ./...)
done

"${ROOT_DIR}/samples/run_samples.sh"
