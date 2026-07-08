param(
    [string] $Configuration = $env:CONFIGURATION
)

$ErrorActionPreference = "Stop"

if (-not $Configuration) {
    $Configuration = "Release"
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = (git -C $ScriptDir rev-parse --show-toplevel).Trim()

if ($env:ZLINK_LOCAL_PACKAGE_ROOT) {
    $ArtifactRoot = $env:ZLINK_LOCAL_PACKAGE_ROOT
} else {
    $ArtifactRoot = Join-Path $RepoRoot ".artifacts/windows"
}

$OutDir = Join-Path $ArtifactRoot "nuget"
$Project = Join-Path $RepoRoot "bindings/dotnet/src/Zlink/Zlink.csproj"

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

dotnet pack $Project -c $Configuration -o $OutDir
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "-- .NET local NuGet package output: $OutDir"
