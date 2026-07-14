$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "../sample_runner.ps1")

$RunDir = New-SampleRunDirectory "bingo-dotnet"
$RunId = "$PID-$([Guid]::NewGuid().ToString('N'))"
$LogDir = Join-Path $RunDir "logs"
$SampleLogDir = if ($env:BINGO_LOG_DIR) { $env:BINGO_LOG_DIR } else { Join-Path $ScriptDir "logs" }
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
New-Item -ItemType Directory -Force -Path $SampleLogDir | Out-Null
Remove-Item -Path (Join-Path $SampleLogDir "*.log") -Force -ErrorAction SilentlyContinue
$env:BINGO_LOG_DIR = $SampleLogDir
$RedisContainer = $null
$RunSucceeded = $false

function Set-DefaultEnv {
    param([string]$Name, [string]$Value)
    if (-not [Environment]::GetEnvironmentVariable($Name, "Process")) {
        [Environment]::SetEnvironmentVariable($Name, $Value, "Process")
    }
}

function Require-LogCount {
    param(
        [Parameter(Mandatory = $true)][string[]]$Path,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][int]$Expected
    )

    $count = @(Select-String -Path $Path -Pattern $Pattern).Count
    if ($count -ne $Expected) {
        throw "Expected $Expected matches for '$Pattern' in $Path, found $count."
    }
}

function Wait-LogContains {
    param(
        [Parameter(Mandatory = $true)][string[]]$Path,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Description,
        [int]$Attempts = 50
    )

    for ($i = 0; $i -lt $Attempts; $i++) {
        if (Select-String -Path $Path -Pattern $Pattern -Quiet) {
            return
        }
        Start-Sleep -Milliseconds 200
    }

    throw "$Description was not found."
}

function Wait-SampleLogContains {
    param(
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Description,
        [int]$Attempts = 50
    )

    for ($i = 0; $i -lt $Attempts; $i++) {
        $match = Get-ChildItem -Path $SampleLogDir -Filter "*.log" |
            Select-String -Pattern $Pattern -List |
            Select-Object -First 1
        if ($null -ne $match) {
            return
        }
        Start-Sleep -Milliseconds 200
    }

    throw "$Description was not found."
}

try {
    [Environment]::SetEnvironmentVariable("BINGO_REDIS_KEY_PREFIX", "bingo:dotnet:${RunId}:", "Process")

    $basePort = if ($env:BINGO_BASE_PORT) { [int]$env:BINGO_BASE_PORT } else { 0 }
    $ports = New-SamplePorts -Count 22 -BasePort $basePort

    Set-DefaultEnv "BINGO_API_A_CHANNEL_ENDPOINT" "tcp://127.0.0.1:$($ports[2])"
    Set-DefaultEnv "BINGO_PLAY_A_CHANNEL_ENDPOINT" "tcp://127.0.0.1:$($ports[3])"
    Set-DefaultEnv "BINGO_SESSION_A_SPOT_ENDPOINT" "tcp://127.0.0.1:$($ports[4])"
    Set-DefaultEnv "BINGO_SESSION_A_ROUTER_ENDPOINT" "tcp://127.0.0.1:$($ports[5])"
    Set-DefaultEnv "BINGO_SESSION_B_SPOT_ENDPOINT" "tcp://127.0.0.1:$($ports[6])"
    Set-DefaultEnv "BINGO_SESSION_B_ROUTER_ENDPOINT" "tcp://127.0.0.1:$($ports[7])"
    Set-DefaultEnv "BINGO_PLAY_B_CHANNEL_ENDPOINT" "tcp://127.0.0.1:$($ports[8])"
    Set-DefaultEnv "BINGO_PLAY_A_SPOT_ENDPOINT" "tcp://127.0.0.1:$($ports[9])"
    Set-DefaultEnv "BINGO_PLAY_A_SPOT_ROUTER_ENDPOINT" "tcp://127.0.0.1:$($ports[10])"
    Set-DefaultEnv "BINGO_SESSION_A_STREAM_ENDPOINT" "tcp://127.0.0.1:$($ports[11])"
    Set-DefaultEnv "BINGO_SESSION_B_STREAM_ENDPOINT" "tcp://127.0.0.1:$($ports[12])"
    Set-DefaultEnv "BINGO_PLAY_B_SPOT_ENDPOINT" "tcp://127.0.0.1:$($ports[13])"
    Set-DefaultEnv "BINGO_PLAY_B_SPOT_ROUTER_ENDPOINT" "tcp://127.0.0.1:$($ports[14])"
    Set-DefaultEnv "BINGO_API_B_CHANNEL_ENDPOINT" "tcp://127.0.0.1:$($ports[15])"
    $redis = Start-SampleRedisContainer "zlink-bingo-dotnet-redis"
    $RedisContainer = $redis.ContainerId
    $env:BINGO_REDIS_ENDPOINT = $redis.Endpoint
    Wait-SampleTcpEndpoint "redis" "tcp://$env:BINGO_REDIS_ENDPOINT"

    $baseSettings = [ordered]@{
        LogDirectory = $SampleLogDir
        RedisEndpoint = $env:BINGO_REDIS_ENDPOINT
        RedisKeyPrefix = $env:BINGO_REDIS_KEY_PREFIX
        ApiAChannelEndpoint = $env:BINGO_API_A_CHANNEL_ENDPOINT
        ApiBChannelEndpoint = $env:BINGO_API_B_CHANNEL_ENDPOINT
        PlayAChannelEndpoint = $env:BINGO_PLAY_A_CHANNEL_ENDPOINT
        PlayBChannelEndpoint = $env:BINGO_PLAY_B_CHANNEL_ENDPOINT
        PlayASpotEndpoint = $env:BINGO_PLAY_A_SPOT_ENDPOINT
        PlayBSpotEndpoint = $env:BINGO_PLAY_B_SPOT_ENDPOINT
        PlayASpotRouterEndpoint = $env:BINGO_PLAY_A_SPOT_ROUTER_ENDPOINT
        PlayBSpotRouterEndpoint = $env:BINGO_PLAY_B_SPOT_ROUTER_ENDPOINT
        SessionASpotEndpoint = $env:BINGO_SESSION_A_SPOT_ENDPOINT
        SessionBSpotEndpoint = $env:BINGO_SESSION_B_SPOT_ENDPOINT
        SessionARouterEndpoint = $env:BINGO_SESSION_A_ROUTER_ENDPOINT
        SessionBRouterEndpoint = $env:BINGO_SESSION_B_ROUTER_ENDPOINT
        SessionAStreamEndpoint = $env:BINGO_SESSION_A_STREAM_ENDPOINT
        SessionBStreamEndpoint = $env:BINGO_SESSION_B_STREAM_ENDPOINT
    }
    $configFiles = @{}
    foreach ($role in @("api-a", "api-b", "play-a", "play-b", "session-a", "session-b", "client")) {
        $sample = [ordered]@{}
        foreach ($key in $baseSettings.Keys) { $sample[$key] = $baseSettings[$key] }
        $sample.NodeName = if ($role.EndsWith("-b")) { "b" } elseif ($role -eq "client") { "client" } else { "a" }
        $path = Join-Path $RunDir "appsettings.$role.json"
        @{ Sample = $sample } | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 -Path $path
        $configFiles[$role] = $path
    }

    Invoke-SampleDotnetBuild (Join-Path $ScriptDir "Bingo.csproj")


    Start-SampleDotnetAssembly -Name "api-a" -Project (Join-Path $ScriptDir "Server/Api/Bingo.Server.Api.csproj") -LogDirectory $LogDir -Arguments @("--config", $configFiles["api-a"]) | Out-Null
    Wait-SampleTcpEndpoint "api-a" $env:BINGO_API_A_CHANNEL_ENDPOINT
    Start-SampleDotnetAssembly -Name "api-b" -Project (Join-Path $ScriptDir "Server/Api/Bingo.Server.Api.csproj") -LogDirectory $LogDir -Arguments @("--config", $configFiles["api-b"]) | Out-Null
    Wait-SampleTcpEndpoint "api-b" $env:BINGO_API_B_CHANNEL_ENDPOINT

    Start-SampleDotnetAssembly -Name "play-a" -Project (Join-Path $ScriptDir "Server/Play/Bingo.Server.Play.csproj") -LogDirectory $LogDir -Arguments @("--config", $configFiles["play-a"]) | Out-Null
    Wait-SampleTcpEndpoint "play-a" $env:BINGO_PLAY_A_CHANNEL_ENDPOINT
    Wait-SampleTcpEndpoint "play-a-spot-router" $env:BINGO_PLAY_A_SPOT_ROUTER_ENDPOINT
    Wait-SampleTcpEndpoint "play-a-spot-pub" $env:BINGO_PLAY_A_SPOT_ENDPOINT
    Start-SampleDotnetAssembly -Name "play-b" -Project (Join-Path $ScriptDir "Server/Play/Bingo.Server.Play.csproj") -LogDirectory $LogDir -Arguments @("--config", $configFiles["play-b"]) | Out-Null
    Wait-SampleTcpEndpoint "play-b" $env:BINGO_PLAY_B_CHANNEL_ENDPOINT
    Wait-SampleTcpEndpoint "play-b-spot-router" $env:BINGO_PLAY_B_SPOT_ROUTER_ENDPOINT
    Wait-SampleTcpEndpoint "play-b-spot-pub" $env:BINGO_PLAY_B_SPOT_ENDPOINT

    Start-SampleDotnetAssembly -Name "session-a" -Project (Join-Path $ScriptDir "Server/Session/Bingo.Server.Session.csproj") -LogDirectory $LogDir -Arguments @("--config", $configFiles["session-a"]) | Out-Null
    Wait-SampleTcpEndpoint "session-a-router" $env:BINGO_SESSION_A_ROUTER_ENDPOINT
    Wait-SampleTcpEndpoint "session-a-stream" $env:BINGO_SESSION_A_STREAM_ENDPOINT
    Start-SampleDotnetAssembly -Name "session-b" -Project (Join-Path $ScriptDir "Server/Session/Bingo.Server.Session.csproj") -LogDirectory $LogDir -Arguments @("--config", $configFiles["session-b"]) | Out-Null
    Wait-SampleTcpEndpoint "session-b-router" $env:BINGO_SESSION_B_ROUTER_ENDPOINT
    Wait-SampleTcpEndpoint "session-b-stream" $env:BINGO_SESSION_B_STREAM_ENDPOINT

    $clientLog = Join-Path $LogDir "client.log"
    Invoke-SampleDotnetRun -Project (Join-Path $ScriptDir "Client/Bingo.Client.csproj") -Arguments @("--config", $configFiles["client"]) *> $clientLog
    if (-not (Select-String -Path $clientLog -Pattern "bingo=completed" -Quiet)) {
        throw "Bingo client did not complete."
    }
    if (-not (Select-String -Path $clientLog -Pattern "stream-inbound sample=Bingo" -Quiet)) {
        throw "Bingo client did not write stream-inbound marker."
    }
    if (-not (Select-String -Path $clientLog -Pattern "stream-inbound sample=Bingo .* seq=[0-9]" -Quiet)) {
        throw "Bingo client did not write sequenced stream-inbound response marker."
    }
    if (-not (Select-String -Path $clientLog -Pattern "stream-inbound sample=Bingo .* name=.*Notify" -Quiet)) {
        throw "Bingo client did not write stream-inbound push marker."
    }

    $playA = Join-Path $LogDir "play-a.out.log"
    $playB = Join-Path $LogDir "play-b.out.log"
    $playLogs = @($playA, $playB)
    Wait-LogContains $playLogs "bingo room: player record loaded. room=.*actor=player-1, wins=0, losses=0" "player-1 record load evidence"
    Wait-LogContains $playLogs "bingo room: player record loaded. room=.*actor=player-2, wins=0, losses=0" "player-2 record load evidence"
    Wait-LogContains $playLogs "bingo room: result reported. room=.*actor=player-1, won=True, wins=1, losses=0" "player-1 result report evidence"
    Wait-LogContains $playLogs "bingo room: result reported. room=.*actor=player-2, won=False, wins=0, losses=1" "player-2 result report evidence"
    Wait-LogContains $playLogs "bingo observer room: actor left. observedRoom=.*observer=observer" "Observer room leave evidence"
    Wait-LogContains $playLogs "bingo room: actor left. room=.*actor=player-1" "player-1 room leave evidence"
    Wait-LogContains $playLogs "bingo room: actor left. room=.*actor=player-2" "player-2 room leave evidence"
    Wait-LogContains $playLogs "entry spot: actor destroy completed. actor=player-1" "player-1 destroy evidence"
    Wait-LogContains $playLogs "entry spot: actor destroy completed. actor=player-2" "player-2 destroy evidence"
    Require-LogCount -Path $playLogs -Pattern "entry spot: actor destroy completed\. actor=player-1" -Expected 1
    Require-LogCount -Path $playLogs -Pattern "entry spot: actor destroy completed\. actor=player-2" -Expected 1
    Require-LogCount -Path $playLogs -Pattern "entry spot: actor destroy completed\. actor=observer" -Expected 0
    Require-LogCount -Path $playLogs -Pattern "bingo room: player record loaded\." -Expected 2
    Require-LogCount -Path $playLogs -Pattern "bingo room: result reported\." -Expected 2
    Wait-SampleLogContains "message flow" "Bingo message-flow evidence"
    $RunSucceeded = $true
}
finally {
    Stop-SampleProcesses
    if ($RedisContainer) {
        Remove-SampleRedisContainer $RedisContainer
    }
    if (-not $RunSucceeded -or $env:BINGO_KEEP_RUN_DIR -eq "1") {
        Write-Host "runDir=$RunDir"
    }
    else {
        Remove-Item -Recurse -Force $RunDir -ErrorAction SilentlyContinue
    }
}
