#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(git -C "$script_dir" rev-parse --show-toplevel)"
artifact_root="${ZLINK_LOCAL_PACKAGE_ROOT:-$repo_root/.artifacts/wsl}"
configuration="${CONFIGURATION:-Release}"
package_version="${ZLINK_CPP_PACKAGE_VERSION:-$(awk 'index($0, "project(zlink_cpp VERSION ") == 1 { print $3; exit }' "$repo_root/bindings/cpp/CMakeLists.txt")}"
if [ -z "$package_version" ]; then
  echo "Unable to resolve zlink_cpp package version" >&2
  exit 2
fi
build_dir="${ZLINK_CPP_LOCAL_BUILD_DIR:-$artifact_root/build/bindings-cpp-$package_version}"
install_prefix="${ZLINK_CPP_INSTALL_PREFIX:-$artifact_root/install/zlink-cpp/$package_version}"

cmake -S "$repo_root/bindings/cpp" -B "$build_dir" \
  -DCMAKE_INSTALL_PREFIX="$install_prefix" \
  -DZLINK_CPP_BUILD_TESTS=OFF \
  -DZLINK_CPP_BUILD_SAMPLES=OFF \
  "$@"

if grep -q '^CMAKE_CONFIGURATION_TYPES:' "$build_dir/CMakeCache.txt"; then
  cmake --build "$build_dir" --config "$configuration"
  cmake --install "$build_dir" --config "$configuration"
else
  cmake --build "$build_dir"
  cmake --install "$build_dir"
fi

echo "-- C++ local package version: $package_version"
echo "-- C++ local install prefix: $install_prefix"
