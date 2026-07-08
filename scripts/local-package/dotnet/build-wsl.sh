#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(git -C "$script_dir" rev-parse --show-toplevel)"
artifact_root="${ZLINK_LOCAL_PACKAGE_ROOT:-$repo_root/.artifacts/wsl}"
configuration="${CONFIGURATION:-Release}"
out_dir="$artifact_root/nuget"
project="$repo_root/bindings/dotnet/src/Zlink/Zlink.csproj"

mkdir -p "$out_dir"

dotnet pack "$project" \
  -c "$configuration" \
  -o "$out_dir" \
  "$@"

echo "-- .NET local NuGet package output: $out_dir"
