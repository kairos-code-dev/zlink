#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(git -C "$script_dir" rev-parse --show-toplevel)"
artifact_root="${ZLINK_LOCAL_PACKAGE_ROOT:-$repo_root/.artifacts/wsl}"
out_dir="$artifact_root/maven"
bindings_dir="$repo_root/bindings/java"

mkdir -p "$out_dir"
out_dir="$(cd "$out_dir" && pwd -P)"

(
  cd "$bindings_dir"
  MAVEN_REPOSITORY_URL="file://$out_dir" ./gradlew publish "$@"
)

echo "-- Java local Maven repository output: $out_dir"
