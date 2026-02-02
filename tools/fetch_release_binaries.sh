#!/usr/bin/env bash
set -euo pipefail

if [ $# -lt 1 ]; then
  echo "usage: $0 <release-url-or-tag>" >&2
  exit 1
fi

input="$1"
repo="ulala-x/zlink"

tag="$input"
if [[ "$input" == *"/releases/tag/"* ]]; then
  tag="${input##*/}"
fi

if ! command -v gh >/dev/null 2>&1; then
  echo "gh CLI required" >&2
  exit 1
fi

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
workdir="/tmp/zlink_release_fetch"
rm -rf "$workdir"
mkdir -p "$workdir"

pushd "$workdir" >/dev/null

gh release download "$tag" -R "$repo"

mkdir -p extracted
shopt -s nullglob
for f in libzlink-*.tar.gz; do
  tar -xzf "$f" -C extracted
done

for pkg in zlink-node-*.tar.gz; do
  tar -xzf "$pkg" -C extracted
done

copy() { src="$1" dst="$2"; if [ -f "$src" ]; then install -D -m 0755 "$src" "$dst"; fi }

cd extracted

# Java
copy libzlink-linux-x64/libzlink.so "$repo_root/bindings/java/src/main/resources/native/linux-x86_64/libzlink.so"
copy libzlink-linux-arm64/libzlink.so "$repo_root/bindings/java/src/main/resources/native/linux-aarch64/libzlink.so"
copy libzlink-macos-x64/libzlink.dylib "$repo_root/bindings/java/src/main/resources/native/darwin-x86_64/libzlink.dylib"
copy libzlink-macos-arm64/libzlink.dylib "$repo_root/bindings/java/src/main/resources/native/darwin-aarch64/libzlink.dylib"
copy libzlink-windows-x64/bin/libzlink-v143-mt-0_6_0.dll "$repo_root/bindings/java/src/main/resources/native/windows-x86_64/zlink.dll"
copy libzlink-windows-arm64/bin/libzlink-v143-mt-0_6_0.dll "$repo_root/bindings/java/src/main/resources/native/windows-aarch64/zlink.dll"

# Python
copy libzlink-linux-x64/libzlink.so "$repo_root/bindings/python/src/zlink/native/linux-x86_64/libzlink.so"
copy libzlink-linux-arm64/libzlink.so "$repo_root/bindings/python/src/zlink/native/linux-aarch64/libzlink.so"
copy libzlink-macos-x64/libzlink.dylib "$repo_root/bindings/python/src/zlink/native/darwin-x86_64/libzlink.dylib"
copy libzlink-macos-arm64/libzlink.dylib "$repo_root/bindings/python/src/zlink/native/darwin-aarch64/libzlink.dylib"
copy libzlink-windows-x64/bin/libzlink-v143-mt-0_6_0.dll "$repo_root/bindings/python/src/zlink/native/windows-x86_64/zlink.dll"
copy libzlink-windows-arm64/bin/libzlink-v143-mt-0_6_0.dll "$repo_root/bindings/python/src/zlink/native/windows-aarch64/zlink.dll"

# .NET
copy libzlink-linux-x64/libzlink.so "$repo_root/bindings/dotnet/runtimes/linux-x64/native/libzlink.so"
copy libzlink-linux-arm64/libzlink.so "$repo_root/bindings/dotnet/runtimes/linux-arm64/native/libzlink.so"
copy libzlink-macos-x64/libzlink.dylib "$repo_root/bindings/dotnet/runtimes/osx-x64/native/libzlink.dylib"
copy libzlink-macos-arm64/libzlink.dylib "$repo_root/bindings/dotnet/runtimes/osx-arm64/native/libzlink.dylib"
copy libzlink-windows-x64/bin/libzlink-v143-mt-0_6_0.dll "$repo_root/bindings/dotnet/runtimes/win-x64/native/zlink.dll"
copy libzlink-windows-arm64/bin/libzlink-v143-mt-0_6_0.dll "$repo_root/bindings/dotnet/runtimes/win-arm64/native/zlink.dll"

# Node (libzlink only; zlink.node comes from node prebuilds job)
copy libzlink-linux-x64/libzlink.so "$repo_root/bindings/node/prebuilds/linux-x64/libzlink.so"
copy libzlink-linux-arm64/libzlink.so "$repo_root/bindings/node/prebuilds/linux-arm64/libzlink.so"
copy libzlink-macos-x64/libzlink.dylib "$repo_root/bindings/node/prebuilds/darwin-x64/libzlink.dylib"
copy libzlink-macos-arm64/libzlink.dylib "$repo_root/bindings/node/prebuilds/darwin-arm64/libzlink.dylib"
copy libzlink-windows-x64/bin/libzlink-v143-mt-0_6_0.dll "$repo_root/bindings/node/prebuilds/win32-x64/zlink.dll"
copy libzlink-windows-arm64/bin/libzlink-v143-mt-0_6_0.dll "$repo_root/bindings/node/prebuilds/win32-arm64/zlink.dll"

copy zlink-node-linux-x64/zlink.node "$repo_root/bindings/node/prebuilds/linux-x64/zlink.node"
copy zlink-node-linux-arm64/zlink.node "$repo_root/bindings/node/prebuilds/linux-arm64/zlink.node"
copy zlink-node-darwin-x64/zlink.node "$repo_root/bindings/node/prebuilds/darwin-x64/zlink.node"
copy zlink-node-darwin-arm64/zlink.node "$repo_root/bindings/node/prebuilds/darwin-arm64/zlink.node"
copy zlink-node-win32-x64/zlink.node "$repo_root/bindings/node/prebuilds/win32-x64/zlink.node"
copy zlink-node-win32-arm64/zlink.node "$repo_root/bindings/node/prebuilds/win32-arm64/zlink.node"

# C++ bindings
copy libzlink-linux-x64/libzlink.so "$repo_root/bindings/cpp/native/linux-x86_64/libzlink.so"
copy libzlink-linux-arm64/libzlink.so "$repo_root/bindings/cpp/native/linux-aarch64/libzlink.so"
copy libzlink-macos-x64/libzlink.dylib "$repo_root/bindings/cpp/native/darwin-x86_64/libzlink.dylib"
copy libzlink-macos-arm64/libzlink.dylib "$repo_root/bindings/cpp/native/darwin-aarch64/libzlink.dylib"
copy libzlink-windows-x64/bin/libzlink-v143-mt-0_6_0.dll "$repo_root/bindings/cpp/native/windows-x86_64/zlink.dll"
copy libzlink-windows-arm64/bin/libzlink-v143-mt-0_6_0.dll "$repo_root/bindings/cpp/native/windows-aarch64/zlink.dll"

popd >/dev/null

echo "Done: fetched $tag and updated bindings binaries"
