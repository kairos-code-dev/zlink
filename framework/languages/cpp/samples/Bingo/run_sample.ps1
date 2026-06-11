$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$CppRoot = Resolve-Path (Join-Path $ScriptDir "../..")
$BuildDir = if ($env:ZLINK_CPP_BUILD_DIR) { $env:ZLINK_CPP_BUILD_DIR } else { Join-Path $CppRoot "build" }
$BinDir = $BuildDir

if (-not (Test-Path (Join-Path $BinDir "sample_cpp_framework_bingo_registry.exe")) -and
    (Test-Path (Join-Path $BinDir "linux-ninja-debug/sample_cpp_framework_bingo_registry.exe"))) {
    $BinDir = Join-Path $BinDir "linux-ninja-debug"
}

$RegistryBin = Join-Path $BinDir "sample_cpp_framework_bingo_registry.exe"
$ApiBin = Join-Path $BinDir "sample_cpp_framework_bingo_api.exe"
$PlayBin = Join-Path $BinDir "sample_cpp_framework_bingo_play.exe"
$SessionBin = Join-Path $BinDir "sample_cpp_framework_bingo_session.exe"
$ClientBin = Join-Path $BinDir "sample_cpp_framework_bingo_client.exe"

foreach ($Binary in @($RegistryBin, $ApiBin, $PlayBin, $SessionBin, $ClientBin)) {
    if (-not (Test-Path $Binary)) {
        throw "Missing executable: $Binary. Build C++ samples first or set ZLINK_CPP_BUILD_DIR."
    }
}

& $RegistryBin
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $ApiBin
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $PlayBin
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $SessionBin
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "bingo server role smoke completed"
Write-Host "bingo client executable present: $ClientBin"
Write-Host "full client/server self-check is not run: current C++ sample channel requests use the local framework runtime and do not complete across separate sample processes."
