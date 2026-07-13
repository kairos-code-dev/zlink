#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(git -C "$script_dir" rev-parse --show-toplevel)"
artifact_root="${ZLINK_LOCAL_PACKAGE_ROOT:-$repo_root/.artifacts/wsl}"
out_dir="$artifact_root/npm"
bindings_dir="$repo_root/bindings/node"
package_mode="${ZLINK_NODE_PACKAGE_MODE:-source}"
package_version="$(node -p "require('$bindings_dir/package.json').version")"
package_major="${package_version%%.*}"

mkdir -p "$out_dir"

host_arch="$(uname -m)"
case "$host_arch" in
  x86_64|amd64)
    node_arch="x64"
    ;;
  aarch64|arm64)
    node_arch="arm64"
    ;;
  *)
    echo "Unsupported Node local package host architecture: $host_arch" >&2
    exit 2
    ;;
esac

(
  cd "$bindings_dir"
  if [ "${ZLINK_SKIP_NPM_CI:-false}" != "true" ]; then
    npm ci
  fi
  npm run build
  npm run rebuild-native
  mkdir -p "prebuilds/linux-$node_arch"
  cp "build/Release/zlink.node" "prebuilds/linux-$node_arch/zlink.node"
  if [ -f "prebuilds/linux-$node_arch/libzlink.so.$package_version" ]; then
    rm -f "prebuilds/linux-$node_arch/libzlink.so.$package_major"
    cp "prebuilds/linux-$node_arch/libzlink.so.$package_version" "prebuilds/linux-$node_arch/libzlink.so.$package_major"
  elif [ -f "native/linux-$node_arch/libzlink.so.$package_version" ]; then
    rm -f "prebuilds/linux-$node_arch/libzlink.so.$package_major"
    cp "native/linux-$node_arch/libzlink.so.$package_version" "prebuilds/linux-$node_arch/libzlink.so.$package_major"
  fi
  case "$package_mode" in
    source)
      ;;
    prebuild)
      npm run verify:prebuilds
      ;;
    *)
      echo "Unknown ZLINK_NODE_PACKAGE_MODE: $package_mode" >&2
      echo "Expected source or prebuild" >&2
      exit 2
      ;;
  esac
  package_file="$(npm pack --pack-destination "$out_dir" "$@" | tail -n 1)"

  consumer_dir="$(mktemp -d)"
  trap 'rm -rf "$consumer_dir"' EXIT
  (
    cd "$consumer_dir"
    npm init -y >/dev/null
    npm install "$out_dir/$package_file" >/dev/null
    node -e "require('@zlink-systems/zlink')"
  )
  rm -rf "$consumer_dir"
  trap - EXIT
  echo "$package_file"
)

echo "-- Node local npm tarball output: $out_dir"
