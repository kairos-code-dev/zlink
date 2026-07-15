$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "../sample_runner.ps1")

$RunDir = New-SampleRunDirectory "gamequest-dotnet"
$RunId = "$PID-$([Guid]::NewGuid().ToString('N'))"
$RedisContainer = $null
$RunSucceeded = $false
$LogDir = Join-Path $RunDir "logs"
$SampleLogDir = Join-Path $RunDir "sample-logs"
New-Item -ItemType Directory -Force -Path $LogDir, $SampleLogDir | Out-Null

try {
    $ports = New-SamplePorts -Count 21 -BasePort 0

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
    $env:GAMEQUEST_GAMEAPI_A_CHANNEL_ENDPOINT = "tcp://127.0.0.1:$($ports[13])"
    $env:GAMEQUEST_GAMEAPI_B_CHANNEL_ENDPOINT = "tcp://127.0.0.1:$($ports[14])"
    $env:GAMEQUEST_MISSION_A_CHANNEL_ENDPOINT = "tcp://127.0.0.1:$($ports[15])"
    $env:GAMEQUEST_MISSION_B_CHANNEL_ENDPOINT = "tcp://127.0.0.1:$($ports[16])"
    $env:GAMEQUEST_GAMEAPI_A_SPOT_ENDPOINT = "tcp://127.0.0.1:$($ports[17])"
    $env:GAMEQUEST_GAMEAPI_A_SPOT_ROUTER_ENDPOINT = "tcp://127.0.0.1:$($ports[18])"
    $env:GAMEQUEST_GAMEAPI_B_SPOT_ENDPOINT = "tcp://127.0.0.1:$($ports[19])"
    $env:GAMEQUEST_GAMEAPI_B_SPOT_ROUTER_ENDPOINT = "tcp://127.0.0.1:$($ports[20])"

    Invoke-SampleDotnetBuild (Join-Path $ScriptDir "GameQuest.csproj")

    $redis = Start-SampleRedisContainer "zlink-gamequest-dotnet-redis"
    $RedisContainer = $redis.ContainerId
    $env:GAMEQUEST_REDIS_ENDPOINT = $redis.Endpoint
    Wait-SampleTcpEndpoint "redis" "tcp://$env:GAMEQUEST_REDIS_ENDPOINT"
    $baseSettings = [ordered]@{
        LogDirectory = $SampleLogDir
        RedisEndpoint = $env:GAMEQUEST_REDIS_ENDPOINT
        RedisKeyPrefix = $env:GAMEQUEST_REDIS_KEY_PREFIX
        GameApiAHttpBaseUrl = $env:GAMEQUEST_GAMEAPI_A_HTTP_BASE_URL
        GameApiBHttpBaseUrl = $env:GAMEQUEST_GAMEAPI_B_HTTP_BASE_URL
        MissionAHttpBaseUrl = $env:GAMEQUEST_MISSION_A_HTTP_URL
        MissionBHttpBaseUrl = $env:GAMEQUEST_MISSION_B_HTTP_URL
        GameApiAStreamEndpoint = $env:GAMEQUEST_GAMEAPI_A_STREAM_ENDPOINT
        GameApiBStreamEndpoint = $env:GAMEQUEST_GAMEAPI_B_STREAM_ENDPOINT
        GameApiAStreamBindEndpoint = $env:GAMEQUEST_API_A_STREAM_BIND_ENDPOINT
        GameApiBStreamBindEndpoint = $env:GAMEQUEST_API_B_STREAM_BIND_ENDPOINT
        GameApiAChannelEndpoint = $env:GAMEQUEST_GAMEAPI_A_CHANNEL_ENDPOINT
        GameApiBChannelEndpoint = $env:GAMEQUEST_GAMEAPI_B_CHANNEL_ENDPOINT
        MissionAChannelEndpoint = $env:GAMEQUEST_MISSION_A_CHANNEL_ENDPOINT
        MissionBChannelEndpoint = $env:GAMEQUEST_MISSION_B_CHANNEL_ENDPOINT
        MissionASpotEndpoint = $env:GAMEQUEST_MISSION_A_SPOT_ENDPOINT
        MissionASpotRouterEndpoint = $env:GAMEQUEST_MISSION_A_SPOT_ROUTER_ENDPOINT
        MissionBSpotEndpoint = $env:GAMEQUEST_MISSION_B_SPOT_ENDPOINT
        MissionBSpotRouterEndpoint = $env:GAMEQUEST_MISSION_B_SPOT_ROUTER_ENDPOINT
        GameApiASpotEndpoint = $env:GAMEQUEST_GAMEAPI_A_SPOT_ENDPOINT
        GameApiASpotRouterEndpoint = $env:GAMEQUEST_GAMEAPI_A_SPOT_ROUTER_ENDPOINT
        GameApiBSpotEndpoint = $env:GAMEQUEST_GAMEAPI_B_SPOT_ENDPOINT
        GameApiBSpotRouterEndpoint = $env:GAMEQUEST_GAMEAPI_B_SPOT_ROUTER_ENDPOINT
    }
    $configFiles = @{}
    foreach ($instance in @("mission-a", "mission-b", "api-a", "api-b", "client")) {
        $sample = [ordered]@{}
        foreach ($key in $baseSettings.Keys) { $sample[$key] = $baseSettings[$key] }
        $sample.InstanceName = $instance
        $path = Join-Path $RunDir "appsettings.$instance.json"
        @{ Sample = $sample } | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 -Path $path
        $configFiles[$instance] = $path
    }

    Start-SampleDotnetAssembly -Name "mission-a" -Project (Join-Path $ScriptDir "Server/QuestMission/GameQuest.QuestMission.csproj") -LogDirectory $LogDir -Arguments @("--config", $configFiles["mission-a"]) | Out-Null
    Wait-SampleTcpEndpoint "mission-a-spot-router" $env:GAMEQUEST_MISSION_A_SPOT_ROUTER_ENDPOINT
    Wait-SampleTcpEndpoint "mission-a-spot-pub" $env:GAMEQUEST_MISSION_A_SPOT_ENDPOINT
    Wait-SampleTcpEndpoint "mission-a-channel" $env:GAMEQUEST_MISSION_A_CHANNEL_ENDPOINT
    Wait-SampleHttpHealth "mission-a" $env:GAMEQUEST_MISSION_A_HTTP_URL

    Start-SampleDotnetAssembly -Name "mission-b" -Project (Join-Path $ScriptDir "Server/QuestMission/GameQuest.QuestMission.csproj") -LogDirectory $LogDir -Arguments @("--config", $configFiles["mission-b"]) | Out-Null
    Wait-SampleTcpEndpoint "mission-b-spot-router" $env:GAMEQUEST_MISSION_B_SPOT_ROUTER_ENDPOINT
    Wait-SampleTcpEndpoint "mission-b-spot-pub" $env:GAMEQUEST_MISSION_B_SPOT_ENDPOINT
    Wait-SampleTcpEndpoint "mission-b-channel" $env:GAMEQUEST_MISSION_B_CHANNEL_ENDPOINT
    Wait-SampleHttpHealth "mission-b" $env:GAMEQUEST_MISSION_B_HTTP_URL

    Start-SampleDotnetAssembly -Name "api-a" -Project (Join-Path $ScriptDir "Server/GameApi/GameQuest.GameApi.csproj") -LogDirectory $LogDir -Arguments @("--config", $configFiles["api-a"]) | Out-Null
    Wait-SampleTcpEndpoint "api-a-stream" $env:GAMEQUEST_API_A_STREAM_BIND_ENDPOINT
    Wait-SampleTcpEndpoint "api-a-channel" $env:GAMEQUEST_GAMEAPI_A_CHANNEL_ENDPOINT
    Wait-SampleTcpEndpoint "api-a-spot" $env:GAMEQUEST_GAMEAPI_A_SPOT_ENDPOINT
    Wait-SampleTcpEndpoint "api-a-spot-router" $env:GAMEQUEST_GAMEAPI_A_SPOT_ROUTER_ENDPOINT
    Wait-SampleHttpHealth "api-a" $env:GAMEQUEST_GAMEAPI_A_HTTP_BASE_URL

    Start-SampleDotnetAssembly -Name "api-b" -Project (Join-Path $ScriptDir "Server/GameApi/GameQuest.GameApi.csproj") -LogDirectory $LogDir -Arguments @("--config", $configFiles["api-b"]) | Out-Null
    Wait-SampleTcpEndpoint "api-b-stream" $env:GAMEQUEST_API_B_STREAM_BIND_ENDPOINT
    Wait-SampleTcpEndpoint "api-b-channel" $env:GAMEQUEST_GAMEAPI_B_CHANNEL_ENDPOINT
    Wait-SampleTcpEndpoint "api-b-spot" $env:GAMEQUEST_GAMEAPI_B_SPOT_ENDPOINT
    Wait-SampleTcpEndpoint "api-b-spot-router" $env:GAMEQUEST_GAMEAPI_B_SPOT_ROUTER_ENDPOINT
    Wait-SampleHttpHealth "api-b" $env:GAMEQUEST_GAMEAPI_B_HTTP_BASE_URL

    Invoke-SampleDotnetRun -Project (Join-Path $ScriptDir "Client/GameQuest.Client.csproj") -Arguments @("--config", $configFiles["client"])

    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "gamequest api event routed"
    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "gamequest mission processed"
    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "gamequest player quest spot ready"
    Invoke-WebRequest -Method Get -Uri "$($env:GAMEQUEST_MISSION_A_HTTP_URL)/self-check/events" -UseBasicParsing | Select-String -Pattern "QuestReconciled" | Out-Null
    Invoke-WebRequest -Method Post -Uri "$($env:GAMEQUEST_GAMEAPI_A_HTTP_BASE_URL)/self-check/assert" -UseBasicParsing | Select-String -Pattern '"passed":true' | Out-Null
    Assert-SampleLogContains -LogDirectory $SampleLogDir -Pattern "message flow"
    Write-Host "gamequest-server-evidence=completed"
    $RunSucceeded = $true
}
finally {
    Stop-SampleProcesses
    if ($RedisContainer) {
        Remove-SampleRedisContainer $RedisContainer
    }
    if (-not $RunSucceeded -or $env:GAMEQUEST_KEEP_RUN_DIR -eq "1") {
        Write-Host "runDir=$RunDir"
    }
    else {
        Remove-Item -Recurse -Force $RunDir -ErrorAction SilentlyContinue
    }
}
