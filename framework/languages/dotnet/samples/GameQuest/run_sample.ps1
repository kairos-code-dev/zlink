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

    $GAMEQUEST_LOG_DIR = $SampleLogDir
    $GAMEQUEST_REDIS_KEY_PREFIX = "gamequest:dotnet:${RunId}:"
    $GAMEQUEST_GAMEAPI_A_HTTP_BASE_URL = "http://127.0.0.1:$($ports[3])"
    $GAMEQUEST_GAMEAPI_B_HTTP_BASE_URL = "http://127.0.0.1:$($ports[4])"
    $GAMEQUEST_GAMEAPI_A_STREAM_ENDPOINT = "ws://127.0.0.1:$($ports[3])/quest/ws"
    $GAMEQUEST_GAMEAPI_B_STREAM_ENDPOINT = "ws://127.0.0.1:$($ports[4])/quest/ws"
    $GAMEQUEST_API_A_STREAM_BIND_ENDPOINT = "tcp://127.0.0.1:$($ports[5])"
    $GAMEQUEST_API_B_STREAM_BIND_ENDPOINT = "tcp://127.0.0.1:$($ports[6])"
    $GAMEQUEST_MISSION_A_HTTP_URL = "http://127.0.0.1:$($ports[7])"
    $GAMEQUEST_MISSION_B_HTTP_URL = "http://127.0.0.1:$($ports[8])"
    $GAMEQUEST_MISSION_A_SPOT_ENDPOINT = "tcp://127.0.0.1:$($ports[9])"
    $GAMEQUEST_MISSION_A_SPOT_ROUTER_ENDPOINT = "tcp://127.0.0.1:$($ports[10])"
    $GAMEQUEST_MISSION_B_SPOT_ENDPOINT = "tcp://127.0.0.1:$($ports[11])"
    $GAMEQUEST_MISSION_B_SPOT_ROUTER_ENDPOINT = "tcp://127.0.0.1:$($ports[12])"
    $GAMEQUEST_GAMEAPI_A_CHANNEL_ENDPOINT = "tcp://127.0.0.1:$($ports[13])"
    $GAMEQUEST_GAMEAPI_B_CHANNEL_ENDPOINT = "tcp://127.0.0.1:$($ports[14])"
    $GAMEQUEST_MISSION_A_CHANNEL_ENDPOINT = "tcp://127.0.0.1:$($ports[15])"
    $GAMEQUEST_MISSION_B_CHANNEL_ENDPOINT = "tcp://127.0.0.1:$($ports[16])"
    $GAMEQUEST_GAMEAPI_A_SPOT_ENDPOINT = "tcp://127.0.0.1:$($ports[17])"
    $GAMEQUEST_GAMEAPI_A_SPOT_ROUTER_ENDPOINT = "tcp://127.0.0.1:$($ports[18])"
    $GAMEQUEST_GAMEAPI_B_SPOT_ENDPOINT = "tcp://127.0.0.1:$($ports[19])"
    $GAMEQUEST_GAMEAPI_B_SPOT_ROUTER_ENDPOINT = "tcp://127.0.0.1:$($ports[20])"

    Invoke-SampleDotnetBuild (Join-Path $ScriptDir "GameQuest.csproj")

    $redis = Start-SampleRedisContainer "zlink-gamequest-dotnet-redis"
    $RedisContainer = $redis.ContainerId
    $GAMEQUEST_REDIS_ENDPOINT = $redis.Endpoint
    Wait-SampleTcpEndpoint "redis" "tcp://$GAMEQUEST_REDIS_ENDPOINT"
    $baseSettings = [ordered]@{
        LogDirectory = $SampleLogDir
        RedisEndpoint = $GAMEQUEST_REDIS_ENDPOINT
        RedisKeyPrefix = $GAMEQUEST_REDIS_KEY_PREFIX
        GameApiAHttpBaseUrl = $GAMEQUEST_GAMEAPI_A_HTTP_BASE_URL
        GameApiBHttpBaseUrl = $GAMEQUEST_GAMEAPI_B_HTTP_BASE_URL
        MissionAHttpBaseUrl = $GAMEQUEST_MISSION_A_HTTP_URL
        MissionBHttpBaseUrl = $GAMEQUEST_MISSION_B_HTTP_URL
        GameApiAStreamEndpoint = $GAMEQUEST_GAMEAPI_A_STREAM_ENDPOINT
        GameApiBStreamEndpoint = $GAMEQUEST_GAMEAPI_B_STREAM_ENDPOINT
        GameApiAStreamBindEndpoint = $GAMEQUEST_API_A_STREAM_BIND_ENDPOINT
        GameApiBStreamBindEndpoint = $GAMEQUEST_API_B_STREAM_BIND_ENDPOINT
        GameApiAChannelEndpoint = $GAMEQUEST_GAMEAPI_A_CHANNEL_ENDPOINT
        GameApiBChannelEndpoint = $GAMEQUEST_GAMEAPI_B_CHANNEL_ENDPOINT
        MissionAChannelEndpoint = $GAMEQUEST_MISSION_A_CHANNEL_ENDPOINT
        MissionBChannelEndpoint = $GAMEQUEST_MISSION_B_CHANNEL_ENDPOINT
        MissionASpotEndpoint = $GAMEQUEST_MISSION_A_SPOT_ENDPOINT
        MissionASpotRouterEndpoint = $GAMEQUEST_MISSION_A_SPOT_ROUTER_ENDPOINT
        MissionBSpotEndpoint = $GAMEQUEST_MISSION_B_SPOT_ENDPOINT
        MissionBSpotRouterEndpoint = $GAMEQUEST_MISSION_B_SPOT_ROUTER_ENDPOINT
        GameApiASpotEndpoint = $GAMEQUEST_GAMEAPI_A_SPOT_ENDPOINT
        GameApiASpotRouterEndpoint = $GAMEQUEST_GAMEAPI_A_SPOT_ROUTER_ENDPOINT
        GameApiBSpotEndpoint = $GAMEQUEST_GAMEAPI_B_SPOT_ENDPOINT
        GameApiBSpotRouterEndpoint = $GAMEQUEST_GAMEAPI_B_SPOT_ROUTER_ENDPOINT
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
    Wait-SampleTcpEndpoint "mission-a-spot-router" $GAMEQUEST_MISSION_A_SPOT_ROUTER_ENDPOINT
    Wait-SampleTcpEndpoint "mission-a-spot-pub" $GAMEQUEST_MISSION_A_SPOT_ENDPOINT
    Wait-SampleTcpEndpoint "mission-a-channel" $GAMEQUEST_MISSION_A_CHANNEL_ENDPOINT
    Wait-SampleHttpHealth "mission-a" $GAMEQUEST_MISSION_A_HTTP_URL

    Start-SampleDotnetAssembly -Name "mission-b" -Project (Join-Path $ScriptDir "Server/QuestMission/GameQuest.QuestMission.csproj") -LogDirectory $LogDir -Arguments @("--config", $configFiles["mission-b"]) | Out-Null
    Wait-SampleTcpEndpoint "mission-b-spot-router" $GAMEQUEST_MISSION_B_SPOT_ROUTER_ENDPOINT
    Wait-SampleTcpEndpoint "mission-b-spot-pub" $GAMEQUEST_MISSION_B_SPOT_ENDPOINT
    Wait-SampleTcpEndpoint "mission-b-channel" $GAMEQUEST_MISSION_B_CHANNEL_ENDPOINT
    Wait-SampleHttpHealth "mission-b" $GAMEQUEST_MISSION_B_HTTP_URL

    Start-SampleDotnetAssembly -Name "api-a" -Project (Join-Path $ScriptDir "Server/GameApi/GameQuest.GameApi.csproj") -LogDirectory $LogDir -Arguments @("--config", $configFiles["api-a"]) | Out-Null
    Wait-SampleTcpEndpoint "api-a-stream" $GAMEQUEST_API_A_STREAM_BIND_ENDPOINT
    Wait-SampleTcpEndpoint "api-a-channel" $GAMEQUEST_GAMEAPI_A_CHANNEL_ENDPOINT
    Wait-SampleTcpEndpoint "api-a-spot" $GAMEQUEST_GAMEAPI_A_SPOT_ENDPOINT
    Wait-SampleTcpEndpoint "api-a-spot-router" $GAMEQUEST_GAMEAPI_A_SPOT_ROUTER_ENDPOINT
    Wait-SampleHttpHealth "api-a" $GAMEQUEST_GAMEAPI_A_HTTP_BASE_URL

    Start-SampleDotnetAssembly -Name "api-b" -Project (Join-Path $ScriptDir "Server/GameApi/GameQuest.GameApi.csproj") -LogDirectory $LogDir -Arguments @("--config", $configFiles["api-b"]) | Out-Null
    Wait-SampleTcpEndpoint "api-b-stream" $GAMEQUEST_API_B_STREAM_BIND_ENDPOINT
    Wait-SampleTcpEndpoint "api-b-channel" $GAMEQUEST_GAMEAPI_B_CHANNEL_ENDPOINT
    Wait-SampleTcpEndpoint "api-b-spot" $GAMEQUEST_GAMEAPI_B_SPOT_ENDPOINT
    Wait-SampleTcpEndpoint "api-b-spot-router" $GAMEQUEST_GAMEAPI_B_SPOT_ROUTER_ENDPOINT
    Wait-SampleHttpHealth "api-b" $GAMEQUEST_GAMEAPI_B_HTTP_BASE_URL

    Invoke-SampleDotnetRun -Project (Join-Path $ScriptDir "Client/GameQuest.Client.csproj") -Arguments @("--config", $configFiles["client"])

    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "gamequest api event routed"
    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "gamequest mission processed"
    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "gamequest player quest spot ready"
    Invoke-WebRequest -Method Get -Uri "$($GAMEQUEST_MISSION_A_HTTP_URL)/self-check/events" -UseBasicParsing | Select-String -Pattern "QuestReconciled" | Out-Null
    Invoke-WebRequest -Method Post -Uri "$($GAMEQUEST_GAMEAPI_A_HTTP_BASE_URL)/self-check/assert" -UseBasicParsing | Select-String -Pattern '"passed":true' | Out-Null
    Assert-SampleLogContains -LogDirectory $SampleLogDir -Pattern "message flow"
    Write-Host "gamequest-server-evidence=completed"
    $RunSucceeded = $true
}
finally {
    Remove-SampleConfigurationFiles -RunDirectory $RunDir
    Stop-SampleProcesses
    if ($RedisContainer) {
        Remove-SampleRedisContainer $RedisContainer
    }
    if (-not $RunSucceeded -or $GAMEQUEST_KEEP_RUN_DIR -eq "1") {
        Write-Host "runDir=$RunDir"
    }
    else {
        Remove-Item -Recurse -Force $RunDir -ErrorAction SilentlyContinue
    }
}
