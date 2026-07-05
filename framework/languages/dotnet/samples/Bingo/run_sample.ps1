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

function Set-DefaultEnv {
    param([string]$Name, [string]$Value)
    if (-not [Environment]::GetEnvironmentVariable($Name, "Process")) {
        [Environment]::SetEnvironmentVariable($Name, $Value, "Process")
    }
}

function Require-LogCount {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
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
        [Parameter(Mandatory = $true)][string]$Path,
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
    $docker = Get-Command docker -ErrorAction SilentlyContinue
    if ($null -eq $docker) {
        throw "Docker is required to run the Bingo sample."
    }

    $RedisContainer = "zlink-bingo-dotnet-redis-$RunId"
    & docker run -d --rm --name $RedisContainer -p "127.0.0.1::6379" redis:7.2-alpine | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to start Redis container."
    }
    $redisPort = (& docker port $RedisContainer "6379/tcp") -replace '^.*:', ''
    $env:BINGO_REDIS_ENDPOINT = "127.0.0.1:$redisPort"
    Wait-SampleTcpEndpoint "redis" "tcp://$env:BINGO_REDIS_ENDPOINT"

    Invoke-SampleDotnetBuild (Join-Path $ScriptDir "Bingo.csproj")


    Start-SampleDotnetAssembly -Name "api-a" -Project (Join-Path $ScriptDir "Server/Api/Bingo.Server.Api.csproj") -LogDirectory $LogDir -Arguments @("--node", "a") | Out-Null
    Wait-SampleTcpEndpoint "api-a" $env:BINGO_API_A_CHANNEL_ENDPOINT
    Start-SampleDotnetAssembly -Name "api-b" -Project (Join-Path $ScriptDir "Server/Api/Bingo.Server.Api.csproj") -LogDirectory $LogDir -Arguments @("--node", "b") | Out-Null
    Wait-SampleTcpEndpoint "api-b" $env:BINGO_API_B_CHANNEL_ENDPOINT

    Start-SampleDotnetAssembly -Name "play-a" -Project (Join-Path $ScriptDir "Server/Play/Bingo.Server.Play.csproj") -LogDirectory $LogDir -Arguments @("--node", "a") | Out-Null
    Wait-SampleTcpEndpoint "play-a" $env:BINGO_PLAY_A_CHANNEL_ENDPOINT
    Wait-SampleTcpEndpoint "play-a-spot-router" $env:BINGO_PLAY_A_SPOT_ROUTER_ENDPOINT
    Wait-SampleTcpEndpoint "play-a-spot-pub" $env:BINGO_PLAY_A_SPOT_ENDPOINT
    Start-SampleDotnetAssembly -Name "play-b" -Project (Join-Path $ScriptDir "Server/Play/Bingo.Server.Play.csproj") -LogDirectory $LogDir -Arguments @("--node", "b") | Out-Null
    Wait-SampleTcpEndpoint "play-b" $env:BINGO_PLAY_B_CHANNEL_ENDPOINT
    Wait-SampleTcpEndpoint "play-b-spot-router" $env:BINGO_PLAY_B_SPOT_ROUTER_ENDPOINT
    Wait-SampleTcpEndpoint "play-b-spot-pub" $env:BINGO_PLAY_B_SPOT_ENDPOINT

    Start-SampleDotnetAssembly -Name "session-a" -Project (Join-Path $ScriptDir "Server/Session/Bingo.Server.Session.csproj") -LogDirectory $LogDir -Arguments @("--node", "a") | Out-Null
    Wait-SampleTcpEndpoint "session-a-router" $env:BINGO_SESSION_A_ROUTER_ENDPOINT
    Wait-SampleTcpEndpoint "session-a-stream" $env:BINGO_SESSION_A_STREAM_ENDPOINT
    Start-SampleDotnetAssembly -Name "session-b" -Project (Join-Path $ScriptDir "Server/Session/Bingo.Server.Session.csproj") -LogDirectory $LogDir -Arguments @("--node", "b") | Out-Null
    Wait-SampleTcpEndpoint "session-b-router" $env:BINGO_SESSION_B_ROUTER_ENDPOINT
    Wait-SampleTcpEndpoint "session-b-stream" $env:BINGO_SESSION_B_STREAM_ENDPOINT

    $clientLog = Join-Path $LogDir "client.log"
    Invoke-SampleDotnetRun -Project (Join-Path $ScriptDir "Client/Bingo.Client.csproj") -Arguments @("--stream-a-endpoint", $env:BINGO_SESSION_A_STREAM_ENDPOINT, "--stream-b-endpoint", $env:BINGO_SESSION_B_STREAM_ENDPOINT) *> $clientLog
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
    Wait-LogContains $playB "bingo observer room: actor left. observedRoom=.*observer=observer" "Observer room leave evidence"
    Wait-LogContains $playA "bingo room: actor left. room=.*actor=player-1" "player-1 room leave evidence"
    Wait-LogContains $playA "bingo room: actor left. room=.*actor=player-2" "player-2 room leave evidence"
    Require-LogCount -Path $playA -Pattern "entry spot: actor left\. actor=player-1" -Expected 1
    Require-LogCount -Path $playA -Pattern "entry spot: actor left\. actor=player-2" -Expected 1
    Require-LogCount -Path $playA -Pattern "entry spot: actor destroy completed\. actor=player-1" -Expected 1
    Require-LogCount -Path $playA -Pattern "entry spot: actor destroy completed\. actor=player-2" -Expected 1
    Require-LogCount -Path $playB -Pattern "entry spot: actor destroy completed\. actor=observer" -Expected 0
    Wait-SampleLogContains "message flow" "Bingo message-flow evidence"
}
finally {
    Stop-SampleProcesses
    if ($RedisContainer) {
        & docker rm -f $RedisContainer | Out-Null
    }
    if ($env:BINGO_KEEP_RUN_DIR -eq "1") {
        Write-Host "runDir=$RunDir"
    }
    else {
        Remove-Item -Recurse -Force $RunDir -ErrorAction SilentlyContinue
    }
}
