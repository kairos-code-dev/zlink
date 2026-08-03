#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)"
BINDING_ROOT="${REPO_ROOT}/bindings/go"
MODULE_PATH="$(sed -n 's/^module //p' "${BINDING_ROOT}/go.mod" | head -n1)"
CORE_VERSION="$(sed -n 's/^#define ZLINK_VERSION_MAJOR //p' "${BINDING_ROOT}/include/zlink.h" | head -n1).$(sed -n 's/^#define ZLINK_VERSION_MINOR //p' "${BINDING_ROOT}/include/zlink.h" | head -n1).$(sed -n 's/^#define ZLINK_VERSION_PATCH //p' "${BINDING_ROOT}/include/zlink.h" | head -n1)"
PACKAGE_VERSION="v${CORE_VERSION}"
PLATFORMS="linux-x86_64"
OUTPUT_ROOT="${ZLINK_LOCAL_PACKAGE_ROOT:-${REPO_ROOT}/.artifacts/wsl}/go"

usage() {
  cat <<'EOF'
Usage: scripts/local-package/go/build-wsl.sh [options]

Options:
  --package-version VERSION  Go module version, including the v prefix.
  --platforms LIST           Comma-separated native payload directories.
                             Supported: linux-x86_64, linux-aarch64,
                             darwin-x86_64, darwin-aarch64.
  --output-root DIR          Absolute output directory.
  -h, --help

The command creates a standard file proxy layout and runs a clean consumer
without a replace directive. It never uses core/build as a runtime input.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --package-version) PACKAGE_VERSION="${2:-}"; shift 2 ;;
    --platforms) PLATFORMS="${2:-}"; shift 2 ;;
    --output-root) OUTPUT_ROOT="${2:-}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

[[ "${OUTPUT_ROOT}" = /* ]] || { echo "--output-root must be absolute" >&2; exit 2; }
[[ "${PACKAGE_VERSION}" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]] || {
  echo "--package-version must use vMAJOR.MINOR.PATCH" >&2
  exit 2
}
[[ "${MODULE_PATH}" == "zlink.systems/zlink/v11" ]] || {
  echo "Unexpected Go module path: ${MODULE_PATH}" >&2
  exit 1
}
[[ "${PACKAGE_VERSION}" == "v${CORE_VERSION}" ]] || {
  echo "Go package version ${PACKAGE_VERSION} must match Core ${CORE_VERSION}" >&2
  exit 1
}

dir_hash() {
  local root="$1"
  (
    cd "${root}"
    find . -type f -print0 | sort -z | while IFS= read -r -d '' file; do
      printf '%s  %s\n' "$(sha256sum "${file}" | awk '{print $1}')" "${file#./}"
    done
  ) | sha256sum | awk '{print $1}'
}

platform_source_dir() {
  case "$1" in
    linux-x86_64|linux-aarch64|darwin-x86_64|darwin-aarch64)
      printf '%s/native/%s\n' "${BINDING_ROOT}" "$1"
      ;;
    *)
      echo "Unsupported Go package platform: $1" >&2
      exit 2
      ;;
  esac
}

copy_platform_payload() {
  local platform="$1"
  local source_dir
  local target_dir
  source_dir="$(platform_source_dir "${platform}")"
  target_dir="${STAGE_MODULE}/native/${platform}"
  mkdir -p "${target_dir}"
  if [[ "${platform}" == darwin-* ]]; then
    [[ -f "${source_dir}/libzlink.dylib" ]] || {
      echo "Missing Go package runtime: ${source_dir}/libzlink.dylib" >&2
      exit 1
    }
    cp -L "${source_dir}/libzlink.dylib" "${target_dir}/libzlink.dylib"
    return
  fi

  local versioned="${source_dir}/libzlink.so.${CORE_VERSION}"
  local major="${source_dir}/libzlink.so.${CORE_VERSION%%.*}"
  local linker="${source_dir}/libzlink.so"
  for file in "${linker}" "${major}" "${versioned}"; do
    [[ -f "${file}" ]] || {
      echo "Missing Go package runtime: ${file}" >&2
      exit 1
    }
  done
  cp -L "${linker}" "${target_dir}/libzlink.so"
  cp -L "${major}" "${target_dir}/libzlink.so.${CORE_VERSION%%.*}"
  cp -L "${versioned}" "${target_dir}/libzlink.so.${CORE_VERSION}"
}

STAGE_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/zlink-go-package.XXXXXX")"
CONSUMER_ROOT=""
cleanup() {
  local status=$?
  if [[ -n "${CONSUMER_ROOT}" && -d "${CONSUMER_ROOT}" ]]; then
    chmod -R u+w -- "${CONSUMER_ROOT}" 2>/dev/null || true
    rm -rf -- "${CONSUMER_ROOT:?}"
  fi
  if [[ -d "${STAGE_ROOT}" ]]; then
    chmod -R u+w -- "${STAGE_ROOT}" 2>/dev/null || true
    rm -rf -- "${STAGE_ROOT:?}"
  fi
  exit "${status}"
}
trap cleanup EXIT

STAGE_MODULE="${STAGE_ROOT}/${MODULE_PATH}@${PACKAGE_VERSION}"
mkdir -p "${STAGE_MODULE}"
cp -a "${BINDING_ROOT}/." "${STAGE_MODULE}/"
rm -rf -- "${STAGE_MODULE:?}/native"
mkdir -p "${STAGE_MODULE}/native"

IFS=',' read -r -a PLATFORM_LIST <<< "${PLATFORMS}"
for platform in "${PLATFORM_LIST[@]}"; do
  [[ -n "${platform}" ]] || { echo "--platforms contains an empty entry" >&2; exit 2; }
  copy_platform_payload "${platform}"
done

if find "${STAGE_MODULE}" -type f \( -path '*/zlink/service/*' -o -name '*service*.h' \) -print -quit | grep -q .; then
  echo "Service header found in Go module stage" >&2
  exit 1
fi
if find "${STAGE_MODULE}/native" -type f \( -name 'libzlink.so.9*' -o -name 'libzlink.so.10*' \) -print -quit | grep -q .; then
  echo "Old Core runtime found in Go module stage" >&2
  exit 1
fi

PROXY_ROOT="${OUTPUT_ROOT}/proxy"
VERSION_ROOT="${PROXY_ROOT}/${MODULE_PATH}/@v"
MODULE_ZIP="${VERSION_ROOT}/${PACKAGE_VERSION}.zip"
mkdir -p "${VERSION_ROOT}"
(
  cd "${STAGE_ROOT}"
  zip -q -r -X "${MODULE_ZIP}" "${MODULE_PATH}@${PACKAGE_VERSION}"
)
cp "${STAGE_MODULE}/go.mod" "${VERSION_ROOT}/${PACKAGE_VERSION}.mod"
printf '{"Version":"%s","Time":"%s"}\n' \
  "${PACKAGE_VERSION}" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
  > "${VERSION_ROOT}/${PACKAGE_VERSION}.info"

CONSUMER_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/zlink-go-consumer.XXXXXX")"
MODCACHE="${CONSUMER_ROOT}/gomodcache"
GOCACHE_DIR="${CONSUMER_ROOT}/gocache"
cat > "${CONSUMER_ROOT}/go.mod" <<EOF
module zlink-go-clean-consumer

go 1.22

require ${MODULE_PATH} ${PACKAGE_VERSION}
EOF
cat > "${CONSUMER_ROOT}/main.go" <<'EOF'
package main

import (
	"fmt"
	"os"

	zlink "zlink.systems/zlink/v11"
)

func main() {
	version := zlink.RuntimeVersion()
	if version.Major != 11 || version.Minor != 1 || version.Patch != 0 {
		panic(fmt.Sprintf("unexpected runtime version: %+v", version))
	}

	ctx, err := zlink.NewContext()
	if err != nil {
		panic(err)
	}
	defer ctx.Close()
	left, err := ctx.PairSocket()
	if err != nil {
		panic(err)
	}
	defer left.Close()
	right, err := ctx.PairSocket()
	if err != nil {
		panic(err)
	}
	defer right.Close()
	endpoint := "inproc://go-clean-consumer"
	if err := left.Bind(endpoint); err != nil {
		panic(err)
	}
	if err := right.Connect(endpoint); err != nil {
		panic(err)
	}
	message, err := zlink.NewMessageString("clean-consumer")
	if err != nil {
		panic(err)
	}
	if _, err := right.Send().Message(message).Submit(nil); err != nil {
		panic(err)
	}
	var received zlink.Received
	if _, err := left.Recv(&received, zlink.RecvFlagsNone); err != nil {
		panic(err)
	}
	defer received.Close()
	part, err := received.SinglePartOrError()
	if err != nil || string(part.Data()) != "clean-consumer" {
		panic(fmt.Sprintf("unexpected payload: %v", err))
	}
	fmt.Fprintf(os.Stdout, "%d.%d.%d clean-consumer-ok\n", version.Major, version.Minor, version.Patch)
}
EOF

export GOPROXY="file://${PROXY_ROOT},off"
export GOSUMDB=off
export GOMODCACHE="${MODCACHE}"
export GOCACHE="${GOCACHE_DIR}"
(cd "${CONSUMER_ROOT}" && \
  env -u LD_LIBRARY_PATH go mod download "${MODULE_PATH}@${PACKAGE_VERSION}" >/dev/null && \
  env -u LD_LIBRARY_PATH go build -o "${CONSUMER_ROOT}/consumer" .)

if [[ "$(uname -s)" == Linux* ]]; then
  expected_cache_dir="${MODCACHE}/${MODULE_PATH}@${PACKAGE_VERSION}/native/linux-x86_64"
  if printf '%s\n' "${PLATFORMS}" | tr ',' '\n' | grep -qx 'linux-x86_64'; then
    ldd_output="$(ldd "${CONSUMER_ROOT}/consumer")"
    printf '%s\n' "${ldd_output}"
    resolved_runtime="$(printf '%s\n' "${ldd_output}" | sed -n 's/^[[:space:]]*libzlink\.so\.11 => \([^[:space:]]*\).*/\1/p')"
    [[ -n "${resolved_runtime}" ]] || {
      echo "Clean consumer did not resolve libzlink.so.11" >&2
      exit 1
    }
    [[ "$(realpath "${resolved_runtime}")" == "$(realpath "${expected_cache_dir}/libzlink.so.11")" ]] || {
      echo "Clean consumer resolved outside the module cache: ${resolved_runtime}" >&2
      exit 1
    }
  fi
fi
"${CONSUMER_ROOT}/consumer"

mkdir -p "${OUTPUT_ROOT}"
ZIP_SHA256="$(sha256sum "${MODULE_ZIP}" | awk '{print $1}')"
HEADER_SHA256="$(dir_hash "${STAGE_MODULE}/include")"
SOURCE_SHA256="$(dir_hash "${STAGE_MODULE}")"
EVIDENCE="${OUTPUT_ROOT}/go-package-${PACKAGE_VERSION}.json"
MODULE_ZIP="${MODULE_ZIP}" MODULE_ZIP_SHA256="${ZIP_SHA256}" MODULE_PATH="${MODULE_PATH}" PACKAGE_VERSION="${PACKAGE_VERSION}" PLATFORMS="${PLATFORMS}" HEADER_SHA256="${HEADER_SHA256}" SOURCE_SHA256="${SOURCE_SHA256}" EVIDENCE="${EVIDENCE}" node <<'NODE'
const fs = require('fs');
const record = {
  format: 1,
  module: process.env.MODULE_PATH,
  version: process.env.PACKAGE_VERSION,
  moduleZip: process.env.MODULE_ZIP,
  moduleZipSha256: process.env.MODULE_ZIP_SHA256,
  platforms: process.env.PLATFORMS.split(','),
  headerSha256: process.env.HEADER_SHA256,
  sourceSha256: process.env.SOURCE_SHA256,
  cleanConsumer: 'pass',
};
fs.writeFileSync(process.env.EVIDENCE, JSON.stringify(record, null, 2) + '\n');
NODE

echo "module=${MODULE_PATH}"
echo "version=${PACKAGE_VERSION}"
echo "zip=${MODULE_ZIP}"
echo "zip_sha256=${ZIP_SHA256}"
echo "platforms=${PLATFORMS}"
echo "evidence=${EVIDENCE}"
