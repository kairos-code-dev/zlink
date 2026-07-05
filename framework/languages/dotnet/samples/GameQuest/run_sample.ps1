$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "../sample_runner.ps1")

$RunDir = New-SampleRunDirectory "gamequest-dotnet"
$RunId = "$PID-$([Guid]::NewGuid().ToString('N'))"
$RedisContainer = $null
$LogDir = Join-Path $RunDir "logs"
$SampleLogDir = Join-Path $RunDir "sample-logs"
New-Item -ItemType Directory -Force -Path $LogDir, $SampleLogDir | Out-Null

try {
    $ports = New-SamplePorts -Count 16 -BasePort 0

    $env:GAMEQUEST_LOG_DIR = $SampleLogDir
    $env:GAMEQUEST_REDIS_KEY_PREFIX = "gamequest:dotnet:${RunId}:"
    $env:GAMEQUEST_GAMEAPI_A_HTTP_BASE_URL = "http://127.0.0.1:$($ports[3])"
    $env:GAMEQUEST_GAMEAPI_B_HTTP_BASE_URL = "http://127.0.0.1:$($ports[4])"
    $env:GAMEQUEST_GAMEAPI_A_STREAM_ENDPOINT = "ws://127.0.0.1:$($ports[3])/quest/ws"
    $env:GAMEQUEST_GAMEAPI_B_STREAM_ENDPOINT = "ws://127.0.0.1:$($ports[4])/quest/ws"
    $env:GAMEQUEST_API_A_STREAM_BIND_ENDPOINT = "tcp://127.0.0.1:$($ports[5])"
    $env:GAMEQUEST_API_B_STREAM_BIND_ENDPOINT = "tcp://127.0.0.1:$($ports[6])"
    $env:GAMEQUEST_MISSION_A_HTTP_URL = "http://127.0.0.1:$($ports[7])"
    $env:GAMEQUEST_MISSION_B_HTTP_URL = "http://127.0.0.1:$($ports[8])"
    $env:GAMEQUEST_MISSION_A_SPOT_ENDPOINT = "tcp://127.0.0.1:$($ports[9])"
    $env:GAMEQUEST_MISSION_A_SPOT_ROUTER_ENDPOINT = "tcp://127.0.0.1:$($ports[10])"
    $env:GAMEQUEST_MISSION_B_SPOT_ENDPOINT = "tcp://127.0.0.1:$($ports[11])"
    $env:GAMEQUEST_MISSION_B_SPOT_ROUTER_ENDPOINT = "tcp://127.0.0.1:$($ports[12])"

    Invoke-SampleDotnetBuild (Join-Path $ScriptDir "GameQuest.csproj")

    # The sample owns its Redis: a dedicated, throwaway container is the shared
    # location store every server registers into (no registry process exists).
    if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
        throw "Docker is required to run the GameQuest sample (it provisions a dedicated Redis container)."
    }
    $RedisContainer = "zlink-gamequest-dotnet-redis-$RunId"
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

    Invoke-SampleDotnetRun -Project (Join-Path $ScriptDir "Client/GameQuest.Client.csproj")

    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "gamequest api event routed"
    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "gamequest mission processed"
    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "gamequest player quest spot ready"
    Invoke-WebRequest -Method Get -Uri "$($env:GAMEQUEST_MISSION_A_HTTP_URL)/self-check/events" -UseBasicParsing | Select-String -Pattern "QuestProgressReconciledEvent" | Out-Null
    Invoke-WebRequest -Method Post -Uri "$($env:GAMEQUEST_GAMEAPI_A_HTTP_BASE_URL)/self-check/assert" -UseBasicParsing | Select-String -Pattern '"passed":true' | Out-Null
    Assert-SampleLogContains -LogDirectory $SampleLogDir -Pattern "message flow"
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
