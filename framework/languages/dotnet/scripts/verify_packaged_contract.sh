#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOTNET_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$DOTNET_ROOT/../../.." && pwd)"
VERSION="0.0.0-contract.$(date +%s).$$"
WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/zlink-dotnet-contract.XXXXXX")"
PACKAGE_DIR="$WORK_DIR/nuget"
CONSUMER_DIR="$WORK_DIR/consumer"
trap 'rm -rf "$WORK_DIR"' EXIT

PROJECTS=(
  src/Zlink.Framework/Zlink.Framework.csproj
  src/Zlink.Framework.AspNetCore/Zlink.Framework.AspNetCore.csproj
  src/Zlink.Framework.Codecs.MessagePack/Zlink.Framework.Codecs.MessagePack.csproj
  src/Zlink.Framework.Codecs.Protobuf/Zlink.Framework.Codecs.Protobuf.csproj
  src/Zlink.Framework.Locations.Redis/Zlink.Framework.Locations.Redis.csproj
  src/Systems.Zlink.Stream.Connector/Systems.Zlink.Stream.Connector.csproj
)
PACKAGE_IDS=(
  Zlink.Framework
  Zlink.Framework.AspNetCore
  Zlink.Framework.Codecs.MessagePack
  Zlink.Framework.Codecs.Protobuf
  Zlink.Framework.Locations.Redis
  Systems.Zlink.Stream.Connector
)

mkdir -p "$PACKAGE_DIR" "$CONSUMER_DIR"
for project in "${PROJECTS[@]}"; do
  dotnet pack "$DOTNET_ROOT/$project" \
    --configuration Release \
    --output "$PACKAGE_DIR" \
    --property:PackageVersion="$VERSION" \
    --nologo >/dev/null
done

mapfile -t packages < <(find "$PACKAGE_DIR" -maxdepth 1 -type f -name '*.nupkg' ! -name '*.symbols.nupkg' -printf '%f\n' | sort)
if [[ "${#packages[@]}" -ne "${#PACKAGE_IDS[@]}" ]]; then
  printf 'Expected %d packages, found %d:\n%s\n' "${#PACKAGE_IDS[@]}" "${#packages[@]}" "${packages[*]}" >&2
  exit 1
fi
for package_id in "${PACKAGE_IDS[@]}"; do
  package="$PACKAGE_DIR/$package_id.$VERSION.nupkg"
  [[ -f "$package" ]] || { echo "Missing package: $package" >&2; exit 1; }
  unzip -Z1 "$package" | grep -Eq '^lib/net8\.0/.+\.dll$' || {
    echo "Package has no net8.0 assembly: $package" >&2
    exit 1
  }
done

cat >"$CONSUMER_DIR/NuGet.Config" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<configuration>
  <packageSources>
    <clear />
    <add key="contract" value="$PACKAGE_DIR" />
    <add key="bindings" value="$REPO_ROOT/.artifacts/wsl/nuget" />
    <add key="nuget.org" value="https://api.nuget.org/v3/index.json" />
  </packageSources>
</configuration>
EOF

cat >"$CONSUMER_DIR/Consumer.csproj" <<EOF
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net8.0</TargetFramework>
    <ImplicitUsings>enable</ImplicitUsings>
    <Nullable>enable</Nullable>
  </PropertyGroup>
  <ItemGroup>
    <PackageReference Include="Zlink.Framework" Version="$VERSION" />
    <PackageReference Include="Zlink.Framework.AspNetCore" Version="$VERSION" />
    <PackageReference Include="Zlink.Framework.Codecs.MessagePack" Version="$VERSION" />
    <PackageReference Include="Zlink.Framework.Codecs.Protobuf" Version="$VERSION" />
    <PackageReference Include="Zlink.Framework.Locations.Redis" Version="$VERSION" />
    <PackageReference Include="Systems.Zlink.Stream.Connector" Version="$VERSION" />
  </ItemGroup>
</Project>
EOF

cat >"$CONSUMER_DIR/Program.cs" <<'EOF'
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Locations;

var assembly = typeof(IZLinkRequestCall).Assembly;
var removedContracts = new[]
{
    "Zlink.Framework.Contracts.Channels.IZLinkYieldRequestCall",
    "Zlink.Framework.Contracts.Actors.IZLinkActorYieldJoinCall",
    "Zlink.Framework.Contracts.Locations.SpotRef",
    "Zlink.Framework.Contracts.Dispatch.ZLinkDispatchMode",
    "Zlink.Framework.Contracts.Assembly.ZLinkFrameworkAssemblyMarker",
    "Zlink.Framework.Contracts.Codecs.Json.ZLinkJsonCodecNamespace",
    "Zlink.Framework.Contracts.Handlers.ZLinkStreamRawAttribute"
}.Where(name => assembly.GetType(name) is not null).ToArray();
if (removedContracts.Length > 0)
    throw new InvalidOperationException(
        $"Removed public contracts are present in the package: {string.Join(", ", removedContracts)}");
if (!typeof(SpotHandle).IsAbstract
    || typeof(IZLinkSpotHandleResolver).GetMethod("ResolveSpotHandleAsync") is null
    || !typeof(IZLinkActorJoinCall).GetMethods().Any(method => method.Name == "Async"))
    throw new InvalidOperationException("The frozen public contract is missing from the package.");
Console.WriteLine("dotnet packaged contract result=passed");
EOF

dotnet run --project "$CONSUMER_DIR/Consumer.csproj" --configuration Release --nologo
