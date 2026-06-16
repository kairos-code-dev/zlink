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
$CTestBin = if ($env:CTEST_BIN) { $env:CTEST_BIN } else { "ctest" }
$PlayChannelEndpoint = "tcp://127.0.0.1:48104"
$PlayStreamEndpoint = "tcp://127.0.0.1:48112"
$ApiChannelEndpoint = "tcp://127.0.0.1:48103"
$ApiHttpEndpoint = "http://127.0.0.1:48113"

foreach ($Binary in @($PlayBin, $ApiBin, $ClientBin)) {
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
    -R "test_cpp_framework_sample_parity|test_cpp_framework_spot_runtime|test_cpp_framework_ActorGateway_actor_session_relay|sample_smoke_sample_cpp_framework_tictactoe_(play|api)" `
    --output-on-failure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$LogDir = Join-Path ([System.IO.Path]::GetTempPath()) ([System.Guid]::NewGuid().ToString())
New-Item -ItemType Directory -Path $LogDir | Out-Null
$PlayProcess = $null
$ApiProcess = $null
try {
    $PlayProcess = Start-Process -FilePath $PlayBin `
        -ArgumentList "--sample.host.keepRunning", "true" `
        -RedirectStandardOutput (Join-Path $LogDir "play.log") `
        -RedirectStandardError (Join-Path $LogDir "play.err") `
        -PassThru
    $ApiProcess = Start-Process -FilePath $ApiBin `
        -ArgumentList "--sample.host.keepRunning", "true" `
        -RedirectStandardOutput (Join-Path $LogDir "api.log") `
        -RedirectStandardError (Join-Path $LogDir "api.err") `
        -PassThru
    Wait-Port "play-channel" $PlayChannelEndpoint
    Wait-Port "play-stream" $PlayStreamEndpoint
    Wait-Port "api-channel" $ApiChannelEndpoint
    Wait-Port "api-http" $ApiHttpEndpoint
    & $ClientBin *> (Join-Path $LogDir "client.log")
    if ($LASTEXITCODE -ne 0) {
        Get-Content (Join-Path $LogDir "client.log") -ErrorAction SilentlyContinue
        Get-Content (Join-Path $LogDir "play.log") -ErrorAction SilentlyContinue
        Get-Content (Join-Path $LogDir "play.err") -ErrorAction SilentlyContinue
        Get-Content (Join-Path $LogDir "api.log") -ErrorAction SilentlyContinue
        Get-Content (Join-Path $LogDir "api.err") -ErrorAction SilentlyContinue
        exit $LASTEXITCODE
    }
}
finally {
    if ($PlayProcess -and -not $PlayProcess.HasExited) {
        Stop-Process -Id $PlayProcess.Id -Force
        Wait-Process -Id $PlayProcess.Id -ErrorAction SilentlyContinue
    }
    if ($ApiProcess -and -not $ApiProcess.HasExited) {
        Stop-Process -Id $ApiProcess.Id -Force
        Wait-Process -Id $ApiProcess.Id -ErrorAction SilentlyContinue
    }
}

Write-Host "tictactoe full client/server self-check completed"
Write-Host "tictactoe actor lifecycle sample gate completed"
