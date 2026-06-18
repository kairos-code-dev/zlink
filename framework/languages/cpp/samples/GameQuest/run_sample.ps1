Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$cppRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)
$buildDir = if ($env:ZLINK_CPP_BUILD_DIR) { $env:ZLINK_CPP_BUILD_DIR } else { Join-Path $cppRoot "build" }
$binary = Join-Path $buildDir "sample_cpp_framework_gamequest_client"
if (-not (Test-Path $binary)) {
    $binary = Join-Path $buildDir "linux-ninja-debug/sample_cpp_framework_gamequest_client"
}
& $binary
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
