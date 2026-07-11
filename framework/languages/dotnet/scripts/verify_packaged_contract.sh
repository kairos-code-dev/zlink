#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOTNET_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$DOTNET_ROOT/../../.." && pwd)"
VERSION="0.0.0-contract.$(date +%s).$$"
WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/zlink-dotnet-contract.XXXXXX")"
PACKAGE_DIR="$WORK_DIR/nuget"
CONSUMER_DIR="$WORK_DIR/consumer"
SOURCE_CONSUMER_DIR="$WORK_DIR/source-consumer"
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

mapfile -t packable_projects < <(
  rg -l '<IsPackable>true</IsPackable>' "$DOTNET_ROOT" -g '*.csproj' \
    | sed "s#^$DOTNET_ROOT/##" \
    | sort
)
mapfile -t expected_projects < <(printf '%s\n' "${PROJECTS[@]}" | sort)
if [[ "$(printf '%s\n' "${packable_projects[@]}")" != "$(printf '%s\n' "${expected_projects[@]}")" ]]; then
  printf 'Packable project manifest differs from the frozen manifest.\nExpected:\n%s\nActual:\n%s\n' \
    "$(printf '%s\n' "${expected_projects[@]}")" \
    "$(printf '%s\n' "${packable_projects[@]}")" >&2
  exit 1
fi

mkdir -p "$PACKAGE_DIR" "$CONSUMER_DIR" "$SOURCE_CONSUMER_DIR"
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
  mapfile -t package_assemblies < <(unzip -Z1 "$package" | grep -E '^lib/net8\.0/[^/]+\.dll$' | sort)
  expected_assembly="lib/net8.0/$package_id.dll"
  if [[ "${#package_assemblies[@]}" -ne 1 || "${package_assemblies[0]:-}" != "$expected_assembly" ]]; then
    printf 'Package assembly manifest differs for %s. Expected %s, found: %s\n' \
      "$package_id" "$expected_assembly" "${package_assemblies[*]:-<none>}" >&2
    exit 1
  fi
  nuspec="$(unzip -p "$package" '*.nuspec')"
  grep -Fq "<id>$package_id</id>" <<<"$nuspec" || {
    echo "Package metadata has the wrong id: $package" >&2
    exit 1
  }
  grep -Fq "<version>$VERSION</version>" <<<"$nuspec" || {
    echo "Package metadata has the wrong version: $package" >&2
    exit 1
  }
  printf 'package=%s sha256=%s assembly=%s\n' \
    "$package_id" "$(sha256sum "$package" | cut -d' ' -f1)" "$expected_assembly"
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

cp "$SCRIPT_DIR/PublicContractSnapshot.cs" "$CONSUMER_DIR/PublicContractSnapshot.cs"
cp "$SCRIPT_DIR/PublicContractSnapshot.cs" "$SOURCE_CONSUMER_DIR/PublicContractSnapshot.cs"

cat >"$SOURCE_CONSUMER_DIR/SourceConsumer.csproj" <<EOF
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net8.0</TargetFramework>
    <ImplicitUsings>enable</ImplicitUsings>
    <Nullable>enable</Nullable>
  </PropertyGroup>
  <ItemGroup>
    <ProjectReference Include="$DOTNET_ROOT/src/Zlink.Framework/Zlink.Framework.csproj" />
    <ProjectReference Include="$DOTNET_ROOT/src/Zlink.Framework.AspNetCore/Zlink.Framework.AspNetCore.csproj" />
    <ProjectReference Include="$DOTNET_ROOT/src/Zlink.Framework.Codecs.MessagePack/Zlink.Framework.Codecs.MessagePack.csproj" />
    <ProjectReference Include="$DOTNET_ROOT/src/Zlink.Framework.Codecs.Protobuf/Zlink.Framework.Codecs.Protobuf.csproj" />
    <ProjectReference Include="$DOTNET_ROOT/src/Zlink.Framework.Locations.Redis/Zlink.Framework.Locations.Redis.csproj" />
    <ProjectReference Include="$DOTNET_ROOT/src/Systems.Zlink.Stream.Connector/Systems.Zlink.Stream.Connector.csproj" />
  </ItemGroup>
</Project>
EOF

cat >"$SOURCE_CONSUMER_DIR/Program.cs" <<'EOF'
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Codecs.MessagePack;
using Zlink.Framework.Codecs.Protobuf;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Locations.Redis;

var assemblies = new[]
{
    typeof(IZLinkRequestCall).Assembly,
    typeof(ServiceCollectionExtensions).Assembly,
    typeof(ZLinkMessagePackCodec).Assembly,
    typeof(ZLinkProtobufCodec).Assembly,
    typeof(ZLinkRedisLocationStore).Assembly,
    typeof(IZlinkStreamConnector).Assembly
};
File.WriteAllText(args[0], PublicContractSnapshot.Render(assemblies));
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
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Codecs.MessagePack;
using Zlink.Framework.Codecs.Protobuf;
using Zlink.Framework.Locations.Redis;

var assembly = typeof(IZLinkRequestCall).Assembly;
var removedContracts = new[]
{
    "Zlink.Framework.Contracts.Channels.IZLinkYieldRequestCall",
    "Zlink.Framework.Contracts.Actors.IZLinkActorYieldJoinCall",
    "Zlink.Framework.Contracts.Locations.SpotRef",
    "Zlink.Framework.Contracts.Dispatch.ZLinkDispatchMode",
    "Zlink.Framework.Contracts.Assembly.ZLinkFrameworkAssemblyMarker",
    "Zlink.Framework.Contracts.Codecs.Json.ZLinkJsonCodecNamespace",
    "Zlink.Framework.Contracts.Handlers.ZLinkStreamRawAttribute",
    "Zlink.Framework.Contracts.Locations.IZLinkSpotRefResolver",
    "Zlink.Framework.Contracts.Locations.IZLinkActorAddressResolver"
}.Where(name => assembly.GetType(name) is not null).ToArray();
if (removedContracts.Length > 0)
    throw new InvalidOperationException(
        $"Removed public contracts are present in the package: {string.Join(", ", removedContracts)}");
if (!typeof(SpotHandle).IsAbstract
    || typeof(IZLinkSpotHandleResolver).GetMethod("ResolveSpotHandleAsync") is null
    || !typeof(IZLinkActorJoinCall).GetMethods().Any(method => method.Name == "Async"))
    throw new InvalidOperationException("The frozen public contract is missing from the package.");
var packagedAssemblies = new[]
{
    typeof(IZLinkRequestCall).Assembly,
    typeof(ServiceCollectionExtensions).Assembly,
    typeof(ZLinkMessagePackCodec).Assembly,
    typeof(ZLinkProtobufCodec).Assembly,
    typeof(ZLinkRedisLocationStore).Assembly,
    typeof(IZlinkStreamConnector).Assembly
};
if (packagedAssemblies.Select(static item => item.GetName().Name).Distinct(StringComparer.Ordinal).Count() != 6)
    throw new InvalidOperationException("Every framework contract package must load its own public assembly.");
if (typeof(ZLinkMessagePackCodec).GetProperty(nameof(ZLinkMessagePackCodec.Default)) is null
    || typeof(ZLinkProtobufCodec).GetProperty(nameof(ZLinkProtobufCodec.Default)) is null
    || typeof(ZLinkRedisLocationStore).GetConstructor([typeof(ZLinkRedisLocationOptions)]) is null
    || typeof(ServiceCollectionExtensions).GetMethod(nameof(ServiceCollectionExtensions.AddZLinkFramework)) is null
    || typeof(ZlinkStreamConnectorFactory).GetMethod(nameof(ZlinkStreamConnectorFactory.Create)) is null)
    throw new InvalidOperationException("A supporting package public entry point is missing.");
File.WriteAllText(args[0], PublicContractSnapshot.Render(packagedAssemblies));
Console.WriteLine("dotnet packaged contract result=passed");
EOF

dotnet run --project "$SOURCE_CONSUMER_DIR/SourceConsumer.csproj" \
  --configuration Release -- "$WORK_DIR/source-api.txt"
dotnet run --project "$CONSUMER_DIR/Consumer.csproj" \
  --configuration Release -- "$WORK_DIR/package-api.txt"
if ! diff -u "$WORK_DIR/source-api.txt" "$WORK_DIR/package-api.txt" >"$WORK_DIR/public-api.diff"; then
  echo "Packaged public API differs from the validated source assemblies:" >&2
  cat "$WORK_DIR/public-api.diff" >&2
  exit 1
fi
printf 'public_api_snapshot_sha256=%s\n' \
  "$(sha256sum "$WORK_DIR/package-api.txt" | cut -d' ' -f1)"
