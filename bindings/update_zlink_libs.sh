#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SYNC_SCRIPT="${REPO_ROOT}/core/tools/fetch_release_binaries.sh"

usage() {
  cat <<'USAGE'
Usage:
  bindings/update_zlink_libs.sh <release-url-or-tag> [--repo owner/repo] [--expect-version X.Y.Z]

Examples:
  bindings/update_zlink_libs.sh core/v1.3.0-hotfix1
  bindings/update_zlink_libs.sh https://github.com/kairos-code-dev/zlink/releases/tag/core/v1.3.0-hotfix1
  bindings/update_zlink_libs.sh core/v1.3.0 --repo kairos-code-dev/zlink --expect-version 1.3.0

Description:
  - Downloads zlink release assets.
  - Replaces internal native libraries in all bindings (cpp/dotnet/java/node/python).
  - Verifies detected zlink_version from linux-x64 libraries matches expected version.

Notes:
  - Requires GitHub CLI (`gh`) authentication.
  - `--repo` is optional. If omitted, repository is resolved from git origin.
  - Expected version is mandatory for safety:
      - Use --expect-version X.Y.Z, or
      - let the script infer X.Y.Z from release tag/url.
USAGE
}

release_ref=""
repo_override="${ZLINK_RELEASE_REPO:-}"
expect_version=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    --repo)
      if [[ $# -lt 2 ]]; then
        echo "Error: --repo requires owner/repo" >&2
        exit 1
      fi
      repo_override="$2"
      shift
      ;;
    --expect-version)
      if [[ $# -lt 2 ]]; then
        echo "Error: --expect-version requires X.Y.Z" >&2
        exit 1
      fi
      expect_version="$2"
      shift
      ;;
    --*)
      echo "Error: unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
    *)
      if [[ -n "${release_ref}" ]]; then
        echo "Error: release reference already set: ${release_ref}" >&2
        usage >&2
        exit 1
      fi
      release_ref="$1"
      ;;
  esac
  shift
done

if [[ -z "${release_ref}" ]]; then
  echo "Error: release-url-or-tag is required" >&2
  usage >&2
  exit 1
fi

if [[ ! -f "${SYNC_SCRIPT}" ]]; then
  echo "Error: sync script not found: ${SYNC_SCRIPT}" >&2
  exit 1
fi

if ! command -v gh >/dev/null 2>&1; then
  echo "Error: gh CLI is required." >&2
  exit 1
fi

extract_tag() {
  local ref="$1"
  if [[ "${ref}" == *"/releases/tag/"* ]]; then
    echo "${ref##*/}"
  else
    echo "${ref}"
  fi
}

resolve_repo() {
  if [[ -n "${repo_override}" ]]; then
    echo "${repo_override}"
    return
  fi

  local origin_url repo_path
  origin_url="$(git -C "${REPO_ROOT}" remote get-url origin 2>/dev/null || true)"
  if [[ -z "${origin_url}" ]]; then
    return
  fi
  repo_path="${origin_url}"
  repo_path="${repo_path#*://}"
  repo_path="${repo_path#*@}"
  if [[ "${repo_path}" == *:* ]]; then
    repo_path="${repo_path#*:}"
  else
    repo_path="${repo_path#*/}"
  fi
  repo_path="${repo_path%.git}"
  if [[ "${repo_path}" =~ ^[^/]+/[^/]+$ ]]; then
    echo "${repo_path}"
  fi
}

tag_name="$(extract_tag "${release_ref}")"
repo_name="$(resolve_repo)"
if [[ -z "${repo_name}" ]]; then
  echo "Error: unable to resolve repository. Use --repo owner/repo." >&2
  exit 1
fi

if [[ -z "${expect_version}" ]]; then
  if [[ "${tag_name}" =~ ([0-9]+\.[0-9]+\.[0-9]+) ]]; then
    expect_version="${BASH_REMATCH[1]}"
  else
    echo "Error: cannot infer expected version from '${tag_name}'. Use --expect-version X.Y.Z." >&2
    exit 1
  fi
fi

if [[ ! "${expect_version}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Error: invalid --expect-version '${expect_version}', expected X.Y.Z." >&2
  exit 1
fi

assets="$(gh release view "${tag_name}" -R "${repo_name}" --json assets --jq '.assets[].name')"
required_assets=(
  "libzlink-linux-x64.tar.gz"
  "libzlink-linux-arm64.tar.gz"
  "libzlink-macos-x64.tar.gz"
  "libzlink-macos-arm64.tar.gz"
  "libzlink-windows-x64.tar.gz"
  "libzlink-windows-arm64.tar.gz"
)
missing_assets=()
for a in "${required_assets[@]}"; do
  if ! grep -Fxq "${a}" <<< "${assets}"; then
    missing_assets+=("${a}")
  fi
done
if [[ "${#missing_assets[@]}" -gt 0 ]]; then
  echo "Error: release '${tag_name}' (${repo_name}) is missing required core assets:" >&2
  printf '  - %s\n' "${missing_assets[@]}" >&2
  exit 1
fi

if [[ -n "${repo_override}" ]]; then
  export ZLINK_RELEASE_REPO="${repo_override}"
fi

echo "[1/2] Syncing bindings native libraries from release: ${release_ref}"
bash "${SYNC_SCRIPT}" "${release_ref}"

echo "[2/2] Verifying linux-x64 zlink_version == ${expect_version}"
py_bin=""
if command -v python3 >/dev/null 2>&1; then
  py_bin="python3"
elif command -v python >/dev/null 2>&1; then
  py_bin="python"
else
  echo "Error: python/python3 is required for version verification." >&2
  exit 1
fi

REPO_ROOT="${REPO_ROOT}" EXPECT_VERSION="${expect_version}" "${py_bin}" - <<'PY'
import ctypes
import os
import sys

repo_root = os.environ["REPO_ROOT"]
expect = os.environ["EXPECT_VERSION"]

libs = {
    "python": os.path.join(repo_root, "bindings/python/src/zlink/native/linux-x86_64/libzlink.so"),
    "node": os.path.join(repo_root, "bindings/node/prebuilds/linux-x64/libzlink.so"),
    "dotnet": os.path.join(repo_root, "bindings/dotnet/runtimes/linux-x64/native/libzlink.so"),
    "java": os.path.join(repo_root, "bindings/java/src/main/resources/native/linux-x86_64/libzlink.so"),
    "cpp": os.path.join(repo_root, "bindings/cpp/native/linux-x86_64/libzlink.so"),
}

failed = False
for name, path in libs.items():
    if not os.path.exists(path):
        print(f"{name}: MISSING ({path})")
        failed = True
        continue
    lib = ctypes.CDLL(os.path.abspath(path))
    fn = lib.zlink_version
    fn.argtypes = [
        ctypes.POINTER(ctypes.c_int),
        ctypes.POINTER(ctypes.c_int),
        ctypes.POINTER(ctypes.c_int),
    ]
    fn.restype = None
    major, minor, patch = ctypes.c_int(), ctypes.c_int(), ctypes.c_int()
    fn(ctypes.byref(major), ctypes.byref(minor), ctypes.byref(patch))
    actual = f"{major.value}.{minor.value}.{patch.value}"
    if actual != expect:
        print(f"{name}: MISMATCH (actual={actual}, expected={expect})")
        failed = True
    else:
        print(f"{name}: {actual}")

if failed:
    sys.exit(1)
PY

echo "Done: bindings native libraries updated and verified (${expect_version})."
