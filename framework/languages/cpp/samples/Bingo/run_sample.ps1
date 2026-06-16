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
$CTestBin = if ($env:CTEST_BIN) { $env:CTEST_BIN } else { "ctest" }
$RegistryPubEndpoint = "tcp://127.0.0.1:47101"
$RegistryRouterEndpoint = "tcp://127.0.0.1:47102"
$ApiChannelEndpoint = "tcp://127.0.0.1:47103"
$PlayChannelEndpoint = "tcp://127.0.0.1:47104"
$SessionStreamEndpoint = "tcp://127.0.0.1:47114"

foreach ($Binary in @($RegistryBin, $ApiBin, $PlayBin, $SessionBin, $ClientBin)) {
    if (-not (Test-Path $Binary)) {
        throw "Missing executable: $Binary. Build C++ samples first or set ZLINK_CPP_BUILD_DIR."
    }
}

function Get-EndpointParts([string]$Endpoint) {
    $value = $Endpoint -replace '^tcp://', '' -replace '^http://', ''
    $index = $value.LastIndexOf(':')
    return @{ Host = $value.Substring(0, $index); Port = [int]$value.Substring($index + 1) }
}

function Wait-Port([string]$Name, [string]$Endpoint) {
    $parts = Get-EndpointParts $Endpoint
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while ([DateTime]::UtcNow -lt $deadline) {
        $client = [System.Net.Sockets.TcpClient]::new()
        try {
            $task = $client.ConnectAsync($parts.Host, $parts.Port)
            if ($task.Wait(100)) {
                return
            }
        }
        catch {
        }
        finally {
            $client.Dispose()
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for $Name at $Endpoint"
}

& $CTestBin --test-dir $BuildDir `
    -R "test_cpp_framework_sample_parity|test_cpp_framework_spot_runtime|test_cpp_framework_ActorGateway_actor_session_relay|sample_smoke_sample_cpp_framework_bingo_(registry|api|play|session)" `
    --output-on-failure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$LogDir = New-Item -ItemType Directory -Path ([System.IO.Path]::Combine([System.IO.Path]::GetTempPath(), [System.IO.Path]::GetRandomFileName()))
$Processes = @()
try {
    $Processes += Start-Process -FilePath $RegistryBin -ArgumentList "--sample.host.keepRunning", "true" -RedirectStandardOutput (Join-Path $LogDir "registry.log") -RedirectStandardError (Join-Path $LogDir "registry.err") -PassThru
    $Processes += Start-Process -FilePath $ApiBin -ArgumentList "--sample.host.keepRunning", "true" -RedirectStandardOutput (Join-Path $LogDir "api.log") -RedirectStandardError (Join-Path $LogDir "api.err") -PassThru
    $Processes += Start-Process -FilePath $PlayBin -ArgumentList "--sample.host.keepRunning", "true" -RedirectStandardOutput (Join-Path $LogDir "play.log") -RedirectStandardError (Join-Path $LogDir "play.err") -PassThru
    $Processes += Start-Process -FilePath $SessionBin -ArgumentList "--sample.host.keepRunning", "true" -RedirectStandardOutput (Join-Path $LogDir "session.log") -RedirectStandardError (Join-Path $LogDir "session.err") -PassThru
    Wait-Port "registry-pub" $RegistryPubEndpoint
    Wait-Port "registry-router" $RegistryRouterEndpoint
    Wait-Port "api-channel" $ApiChannelEndpoint
    Wait-Port "play-channel" $PlayChannelEndpoint
    Wait-Port "session-stream" $SessionStreamEndpoint

    & $ClientBin *> (Join-Path $LogDir "client.log")
    if ($LASTEXITCODE -ne 0) {
        Get-Content (Join-Path $LogDir "client.log") -ErrorAction SilentlyContinue
        Get-Content (Join-Path $LogDir "session.err") -ErrorAction SilentlyContinue
        Get-Content (Join-Path $LogDir "play.err") -ErrorAction SilentlyContinue
        Get-Content (Join-Path $LogDir "api.err") -ErrorAction SilentlyContinue
        Get-Content (Join-Path $LogDir "registry.err") -ErrorAction SilentlyContinue
        exit $LASTEXITCODE
    }
}
finally {
    foreach ($Process in $Processes) {
        if (-not $Process.HasExited) {
            Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
        }
    }
}

Write-Host "bingo full client/server self-check completed"
Write-Host "bingo actor lifecycle sample gate completed"
