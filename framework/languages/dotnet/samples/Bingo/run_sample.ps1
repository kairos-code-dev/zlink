$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "../sample_runner.ps1")

$RunDir = New-SampleRunDirectory "bingo-dotnet"
$LogDir = Join-Path $RunDir "logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

function Set-DefaultEnv {
    param([string]$Name, [string]$Value)
    if (-not [Environment]::GetEnvironmentVariable($Name, "Process")) {
        [Environment]::SetEnvironmentVariable($Name, $Value, "Process")
    }
}

try {
    $basePort = if ($env:BINGO_BASE_PORT) { [int]$env:BINGO_BASE_PORT } else { 0 }
    $ports = New-SamplePorts -Count 13 -BasePort $basePort

    Set-DefaultEnv "BINGO_REGISTRY_PUB_ENDPOINT" "tcp://127.0.0.1:$($ports[0])"
    Set-DefaultEnv "BINGO_REGISTRY_ROUTER_ENDPOINT" "tcp://127.0.0.1:$($ports[1])"
    Set-DefaultEnv "BINGO_API_CHANNEL_ENDPOINT" "tcp://127.0.0.1:$($ports[2])"
    Set-DefaultEnv "BINGO_PLAY_CHANNEL_ENDPOINT" "tcp://127.0.0.1:$($ports[3])"
    Set-DefaultEnv "BINGO_SESSION_SPOT_ENDPOINT" "tcp://127.0.0.1:$($ports[4])"
    Set-DefaultEnv "BINGO_SESSION_ROUTER_ENDPOINT" "tcp://127.0.0.1:$($ports[5])"
    Set-DefaultEnv "BINGO_RECONNECT_SESSION_SPOT_ENDPOINT" "tcp://127.0.0.1:$($ports[6])"
    Set-DefaultEnv "BINGO_RECONNECT_SESSION_ROUTER_ENDPOINT" "tcp://127.0.0.1:$($ports[7])"
    Set-DefaultEnv "BINGO_PLAY_ROUTER_ENDPOINT" "tcp://127.0.0.1:$($ports[8])"
    Set-DefaultEnv "BINGO_PLAY_SPOT_ENDPOINT" "tcp://127.0.0.1:$($ports[9])"
    Set-DefaultEnv "BINGO_PLAY_SPOT_ROUTER_ENDPOINT" "tcp://127.0.0.1:$($ports[10])"
    Set-DefaultEnv "BINGO_STREAM_ENDPOINT" "tcp://127.0.0.1:$($ports[11])"
    Set-DefaultEnv "BINGO_RECONNECT_STREAM_ENDPOINT" "tcp://127.0.0.1:$($ports[12])"
    Set-DefaultEnv "BINGO_METADATA_DIR" (Join-Path $RunDir "metadata")

    Invoke-SampleDotnetBuild (Join-Path $ScriptDir "Bingo.csproj")

    Start-SampleDotnetAssembly -Name "registry" -Project (Join-Path $ScriptDir "Server/Registry/Bingo.Server.Registry.csproj") -LogDirectory $LogDir | Out-Null
    Wait-SampleTcpEndpoint "registry-router" $env:BINGO_REGISTRY_ROUTER_ENDPOINT

    Start-SampleDotnetAssembly -Name "api" -Project (Join-Path $ScriptDir "Server/Api/Bingo.Server.Api.csproj") -LogDirectory $LogDir | Out-Null
    Wait-SampleTcpEndpoint "api" $env:BINGO_API_CHANNEL_ENDPOINT

    Start-SampleDotnetAssembly -Name "play" -Project (Join-Path $ScriptDir "Server/Play/Bingo.Server.Play.csproj") -LogDirectory $LogDir | Out-Null
    Wait-SampleTcpEndpoint "play" $env:BINGO_PLAY_CHANNEL_ENDPOINT
    Wait-SampleTcpEndpoint "play-spot-router" $env:BINGO_PLAY_SPOT_ROUTER_ENDPOINT
    Wait-SampleTcpEndpoint "play-spot-pub" $env:BINGO_PLAY_SPOT_ENDPOINT

    Start-SampleDotnetAssembly -Name "session" -Project (Join-Path $ScriptDir "Server/Session/Bingo.Server.Session.csproj") -LogDirectory $LogDir | Out-Null
    Wait-SampleTcpEndpoint "session-route" $env:BINGO_SESSION_SPOT_ENDPOINT
    Wait-SampleTcpEndpoint "session-router" $env:BINGO_SESSION_ROUTER_ENDPOINT
    Wait-SampleTcpEndpoint "session-stream" $env:BINGO_STREAM_ENDPOINT

    Invoke-SampleDotnetRun -Project (Join-Path $ScriptDir "Probe/Bingo.Probe.csproj") -Arguments @("--registry-endpoint", $env:BINGO_REGISTRY_ROUTER_ENDPOINT, "--timeout-seconds", "10")
    $clientLog = Join-Path $LogDir "client.log"
    Invoke-SampleDotnetRun -Project (Join-Path $ScriptDir "Client/Bingo.Client.csproj") -Arguments @("--stream-endpoint", $env:BINGO_STREAM_ENDPOINT) *> $clientLog
    if (-not (Select-String -Path $clientLog -Pattern "stream-inbound sample=Bingo" -Quiet)) {
        throw "Bingo client did not write stream-inbound marker."
    }
    if (-not (Select-String -Path $clientLog -Pattern "stream-inbound sample=Bingo .* seq=[0-9]" -Quiet)) {
        throw "Bingo client did not write sequenced stream-inbound response marker."
    }
    if (-not (Select-String -Path $clientLog -Pattern "stream-inbound sample=Bingo .* name=.*Notify" -Quiet)) {
        throw "Bingo client did not write stream-inbound push marker."
    }
}
finally {
    Stop-SampleProcesses
    if ($env:BINGO_KEEP_RUN_DIR -eq "1") {
        Write-Host "runDir=$RunDir"
    }
    else {
        Remove-Item -Recurse -Force $RunDir -ErrorAction SilentlyContinue
    }
}
