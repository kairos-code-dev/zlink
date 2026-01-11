param(
    [string]$BuildType = "Release"
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $ScriptDir)

# Find the build directory
$BuildDir = $null
if (Test-Path (Join-Path $ProjectRoot "build\windows-x64")) {
    $BuildDir = Join-Path $ProjectRoot "build\windows-x64"
} elseif (Test-Path (Join-Path $ProjectRoot "build\windows-arm64")) {
    $BuildDir = Join-Path $ProjectRoot "build\windows-arm64"
} else {
    Write-Error "Build directory not found"
    exit 1
}

Write-Host "=== Running tests ==="
Write-Host "Build directory: $BuildDir"

Push-Location $BuildDir

try {
    ctest --build-config $BuildType --output-on-failure --parallel $env:NUMBER_OF_PROCESSORS
    if ($LASTEXITCODE -ne 0) { throw "Tests failed" }

    Write-Host "=== Tests complete ==="
}
finally {
    Pop-Location
}
