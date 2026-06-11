$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$CppRoot = Resolve-Path (Join-Path $ScriptDir "../..")
$BuildDir = if ($env:ZLINK_CPP_BUILD_DIR) { $env:ZLINK_CPP_BUILD_DIR } else { Join-Path $CppRoot "build" }
$BinDir = $BuildDir

if (-not (Test-Path (Join-Path $BinDir "sample_cpp_framework_tictactoe_play.exe")) -and
    (Test-Path (Join-Path $BinDir "linux-ninja-debug/sample_cpp_framework_tictactoe_play.exe"))) {
    $BinDir = Join-Path $BinDir "linux-ninja-debug"
}

$PlayBin = Join-Path $BinDir "sample_cpp_framework_tictactoe_play.exe"
$ApiBin = Join-Path $BinDir "sample_cpp_framework_tictactoe_api.exe"
$ClientBin = Join-Path $BinDir "sample_cpp_framework_tictactoe_client.exe"

foreach ($Binary in @($PlayBin, $ApiBin, $ClientBin)) {
    if (-not (Test-Path $Binary)) {
        throw "Missing executable: $Binary. Build C++ samples first or set ZLINK_CPP_BUILD_DIR."
    }
}

& $PlayBin
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $ApiBin
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "tictactoe server role smoke completed"
Write-Host "tictactoe client executable present: $ClientBin"
Write-Host "full client/server self-check is not run: current C++ sample channel requests use the local framework runtime and do not complete across separate sample processes."
