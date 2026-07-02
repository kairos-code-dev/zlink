$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "../sample_runner.ps1")

$RunDir = New-SampleRunDirectory "gamequest-dotnet"
$RedisContainer = $null
$LogDir = Join-Path $RunDir "logs"
$StoreDir = Join-Path $RunDir "store"
New-Item -ItemType Directory -Force -Path $LogDir, $StoreDir | Out-Null

function Set-DefaultEnv {
    param([string]$Name, [string]$Value)
    if (-not [Environment]::GetEnvironmentVariable($Name, "Process")) {
        [Environment]::SetEnvironmentVariable($Name, $Value, "Process")
    }
}

try {
    $basePort = if ($env:GAMEQUEST_BASE_PORT) { [int]$env:GAMEQUEST_BASE_PORT } else { 0 }
    $ports = New-SamplePorts -Count 14 -BasePort $basePort

    Set-DefaultEnv "GAMEQUEST_REDIS_KEY_PREFIX" "gamequest:dotnet:${PID}:$([Guid]::NewGuid().ToString('N')):"
    Set-DefaultEnv "GAMEQUEST_FANOUT_PUBLISHER_A_ENDPOINT" "tcp://127.0.0.1:$($ports[2])"
    Set-DefaultEnv "GAMEQUEST_FANOUT_PUBLISHER_B_ENDPOINT" "tcp://127.0.0.1:$($ports[13])"
    Set-DefaultEnv "GAMEQUEST_GAMEAPI_A_HTTP_BASE_URL" "http://127.0.0.1:$($ports[3])"
    Set-DefaultEnv "GAMEQUEST_GAMEAPI_B_HTTP_BASE_URL" "http://127.0.0.1:$($ports[4])"
    Set-DefaultEnv "GAMEQUEST_GAMEAPI_A_STREAM_ENDPOINT" "ws://127.0.0.1:$($ports[3])/quest/ws"
    Set-DefaultEnv "GAMEQUEST_GAMEAPI_B_STREAM_ENDPOINT" "ws://127.0.0.1:$($ports[4])/quest/ws"
    Set-DefaultEnv "GAMEQUEST_API_A_STREAM_BIND_ENDPOINT" "tcp://127.0.0.1:$($ports[5])"
    Set-DefaultEnv "GAMEQUEST_API_B_STREAM_BIND_ENDPOINT" "tcp://127.0.0.1:$($ports[6])"
    Set-DefaultEnv "GAMEQUEST_MISSION_A_HTTP_URL" "http://127.0.0.1:$($ports[7])"
    Set-DefaultEnv "GAMEQUEST_MISSION_B_HTTP_URL" "http://127.0.0.1:$($ports[8])"
    Set-DefaultEnv "GAMEQUEST_MISSION_A_SPOT_ENDPOINT" "tcp://127.0.0.1:$($ports[9])"
    Set-DefaultEnv "GAMEQUEST_MISSION_A_SPOT_ROUTER_ENDPOINT" "tcp://127.0.0.1:$($ports[10])"
    Set-DefaultEnv "GAMEQUEST_MISSION_B_SPOT_ENDPOINT" "tcp://127.0.0.1:$($ports[11])"
    Set-DefaultEnv "GAMEQUEST_MISSION_B_SPOT_ROUTER_ENDPOINT" "tcp://127.0.0.1:$($ports[12])"
    Set-DefaultEnv "GAMEQUEST_STORE_DIR" $StoreDir

    Invoke-SampleDotnetBuild (Join-Path $ScriptDir "GameQuest.csproj")

    # The sample owns its Redis: a dedicated, throwaway container is the shared
    # location store every server registers into (no registry process exists).
    if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
        throw "Docker is required to run the GameQuest sample (it provisions a dedicated Redis container)."
    }
    $RedisContainer = "gamequest-dotnet-redis-$PID-$([Guid]::NewGuid().ToString('N'))"
    & docker run -d --rm --name $RedisContainer -p "127.0.0.1::6379" redis:7.2-alpine | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to start Redis container."
    }
    $redisPort = (& docker port $RedisContainer "6379/tcp") -replace '^.*:', ''
    $env:GAMEQUEST_REDIS_ENDPOINT = "127.0.0.1:$redisPort"
    Wait-SampleTcpEndpoint "redis" "tcp://$env:GAMEQUEST_REDIS_ENDPOINT"

    $env:ASPNETCORE_URLS = $env:GAMEQUEST_MISSION_A_HTTP_URL
    $env:GAMEQUEST_MISSION_NAME = "mission-a"
    Start-SampleDotnetAssembly -Name "mission-a" -Project (Join-Path $ScriptDir "Server/QuestMission/GameQuest.QuestMission.csproj") -LogDirectory $LogDir | Out-Null
    Wait-SampleTcpEndpoint "mission-a-spot-router" $env:GAMEQUEST_MISSION_A_SPOT_ROUTER_ENDPOINT
    Wait-SampleTcpEndpoint "mission-a-spot-pub" $env:GAMEQUEST_MISSION_A_SPOT_ENDPOINT
    Wait-SampleHttpHealth "mission-a" $env:GAMEQUEST_MISSION_A_HTTP_URL

    $env:ASPNETCORE_URLS = $env:GAMEQUEST_MISSION_B_HTTP_URL
    $env:GAMEQUEST_MISSION_NAME = "mission-b"
    Start-SampleDotnetAssembly -Name "mission-b" -Project (Join-Path $ScriptDir "Server/QuestMission/GameQuest.QuestMission.csproj") -LogDirectory $LogDir | Out-Null
    Wait-SampleTcpEndpoint "mission-b-spot-router" $env:GAMEQUEST_MISSION_B_SPOT_ROUTER_ENDPOINT
    Wait-SampleTcpEndpoint "mission-b-spot-pub" $env:GAMEQUEST_MISSION_B_SPOT_ENDPOINT
    Wait-SampleHttpHealth "mission-b" $env:GAMEQUEST_MISSION_B_HTTP_URL

    $env:ASPNETCORE_URLS = $env:GAMEQUEST_GAMEAPI_A_HTTP_BASE_URL
    $env:GAMEQUEST_API_NAME = "api-a"
    $env:GAMEQUEST_STREAM_BIND_ENDPOINT = $env:GAMEQUEST_API_A_STREAM_BIND_ENDPOINT
    Start-SampleDotnetAssembly -Name "api-a" -Project (Join-Path $ScriptDir "Server/GameApi/GameQuest.GameApi.csproj") -LogDirectory $LogDir | Out-Null
    Wait-SampleTcpEndpoint "api-a-stream" $env:GAMEQUEST_API_A_STREAM_BIND_ENDPOINT
    Wait-SampleHttpHealth "api-a" $env:GAMEQUEST_GAMEAPI_A_HTTP_BASE_URL

    $env:ASPNETCORE_URLS = $env:GAMEQUEST_GAMEAPI_B_HTTP_BASE_URL
    $env:GAMEQUEST_API_NAME = "api-b"
    $env:GAMEQUEST_STREAM_BIND_ENDPOINT = $env:GAMEQUEST_API_B_STREAM_BIND_ENDPOINT
    Start-SampleDotnetAssembly -Name "api-b" -Project (Join-Path $ScriptDir "Server/GameApi/GameQuest.GameApi.csproj") -LogDirectory $LogDir | Out-Null
    Wait-SampleTcpEndpoint "api-b-stream" $env:GAMEQUEST_API_B_STREAM_BIND_ENDPOINT
    Wait-SampleHttpHealth "api-b" $env:GAMEQUEST_GAMEAPI_B_HTTP_BASE_URL

    $startupDelaySeconds = if ($env:GAMEQUEST_STARTUP_DELAY_SECONDS) { [double]$env:GAMEQUEST_STARTUP_DELAY_SECONDS } else { 3.0 }
    Start-Sleep -Seconds $startupDelaySeconds
    Invoke-SampleDotnetRun -Project (Join-Path $ScriptDir "Client/GameQuest.Client.csproj")

    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "gamequest api event published"
    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "gamequest mission processed"
    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "gamequest player quest spot ready"
    Select-String -Path (Join-Path $StoreDir "quest-events.json") -Pattern "QuestProgressReconciledEvent" -SimpleMatch | Out-Null
    Invoke-WebRequest -Method Post -Uri "$($env:GAMEQUEST_GAMEAPI_A_HTTP_BASE_URL)/self-check/assert" -UseBasicParsing | Select-String -Pattern '"passed":true' | Out-Null
    Write-Host "gamequest-server-evidence=completed"
}
finally {
    Stop-SampleProcesses
    if ($RedisContainer) {
        & docker rm -f $RedisContainer | Out-Null
    }
    if ($env:GAMEQUEST_KEEP_RUN_DIR -eq "1") {
        Write-Host "runDir=$RunDir"
    }
    else {
        Remove-Item -Recurse -Force $RunDir -ErrorAction SilentlyContinue
    }
}
