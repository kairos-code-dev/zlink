$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "../sample_runner.ps1")

$RunDir = New-SampleRunDirectory "supportchat-dotnet"
$RunId = "$PID-$([Guid]::NewGuid().ToString('N'))"
$RedisContainer = $null
$RunSucceeded = $false
$LogDir = Join-Path $RunDir "logs"
$SampleLogDir = Join-Path $RunDir "sample-logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
New-Item -ItemType Directory -Force -Path $SampleLogDir | Out-Null
$SUPPORTCHAT_LOG_DIR = $SampleLogDir

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
    $ports = New-SamplePorts -Count 7 -BasePort 0

    $SUPPORTCHAT_API_CHANNEL_ENDPOINT = "tcp://127.0.0.1:$($ports[0])"
    $SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT = "tcp://127.0.0.1:$($ports[1])"
    $SUPPORTCHAT_SESSION_SPOT_ENDPOINT = "tcp://127.0.0.1:$($ports[2])"
    $SUPPORTCHAT_SESSION_ROUTER_ENDPOINT = "tcp://127.0.0.1:$($ports[3])"
    $SUPPORTCHAT_ENTRY_SPOT_ENDPOINT = "tcp://127.0.0.1:$($ports[4])"
    $SUPPORTCHAT_ENTRY_SPOT_ROUTER_ENDPOINT = "tcp://127.0.0.1:$($ports[5])"
    $SUPPORTCHAT_STREAM_ENDPOINT = "tcp://127.0.0.1:$($ports[6])"
    $SUPPORTCHAT_REDIS_KEY_PREFIX = "supportchat:dotnet:${RunId}:"

    $redis = Start-SampleRedisContainer "zlink-supportchat-dotnet-redis"
    $RedisContainer = $redis.ContainerId
    $SUPPORTCHAT_REDIS_ENDPOINT = $redis.Endpoint
    Wait-SampleTcpEndpoint "redis" "tcp://$SUPPORTCHAT_REDIS_ENDPOINT"
    $configFile = Join-Path $RunDir "appsettings.json"
    @{
        Sample = [ordered]@{
            LogDirectory = $SampleLogDir
            RedisEndpoint = $SUPPORTCHAT_REDIS_ENDPOINT
            RedisKeyPrefix = $SUPPORTCHAT_REDIS_KEY_PREFIX
            ApiChannelEndpoint = $SUPPORTCHAT_API_CHANNEL_ENDPOINT
            SupportChannelEndpoint = $SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT
            SessionSpotEndpoint = $SUPPORTCHAT_SESSION_SPOT_ENDPOINT
            SessionRouterEndpoint = $SUPPORTCHAT_SESSION_ROUTER_ENDPOINT
            SupportEntrySpotEndpoint = $SUPPORTCHAT_ENTRY_SPOT_ENDPOINT
            SupportEntrySpotRouterEndpoint = $SUPPORTCHAT_ENTRY_SPOT_ROUTER_ENDPOINT
            StreamEndpoint = $SUPPORTCHAT_STREAM_ENDPOINT
        }
    } | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 -Path $configFile

    Invoke-SampleDotnetBuild (Join-Path $ScriptDir "SupportChat.csproj")

    Start-SampleDotnetAssembly -Name "support" -Project (Join-Path $ScriptDir "Server/Support/SupportChat.Server.Support.csproj") -LogDirectory $LogDir -Arguments @("--config", $configFile) | Out-Null
    Wait-SampleTcpEndpoint "support-channel" $SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT
    Wait-SampleTcpEndpoint "support-spot-router" $SUPPORTCHAT_ENTRY_SPOT_ROUTER_ENDPOINT
    Wait-SampleTcpEndpoint "support-spot-pub" $SUPPORTCHAT_ENTRY_SPOT_ENDPOINT

    Start-SampleDotnetAssembly -Name "api" -Project (Join-Path $ScriptDir "Server/Api/SupportChat.Server.Api.csproj") -LogDirectory $LogDir -Arguments @("--config", $configFile) | Out-Null
    Wait-SampleTcpEndpoint "api" $SUPPORTCHAT_API_CHANNEL_ENDPOINT

    Start-SampleDotnetAssembly -Name "session" -Project (Join-Path $ScriptDir "Server/Session/SupportChat.Server.Session.csproj") -LogDirectory $LogDir -Arguments @("--config", $configFile) | Out-Null
    Wait-SampleTcpEndpoint "session-route" $SUPPORTCHAT_SESSION_SPOT_ENDPOINT
    Wait-SampleTcpEndpoint "session-router" $SUPPORTCHAT_SESSION_ROUTER_ENDPOINT
    Wait-SampleTcpEndpoint "session-stream" $SUPPORTCHAT_STREAM_ENDPOINT

    $clientLog = Join-Path $LogDir "client.log"
    Invoke-SampleDotnetRun -Project (Join-Path $ScriptDir "Client/SupportChat.Client.csproj") -Arguments @("--config", $configFile) *> $clientLog
    if (-not (Select-String -Path $clientLog -Pattern "supportchat=completed" -Quiet)) {
        throw "SupportChat client did not complete."
    }
    if (-not (Select-String -Path $clientLog -Pattern "supportchat-closed-typing-ignore=verified" -Quiet)) {
        throw "SupportChat client did not verify closed typing ignore."
    }

    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "support conversation: created"
    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "support conversation: actor joined"
    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "status=WaitingForAgent"
    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "status=Active"
    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "status=WaitingForClose"
    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "status=Closed"
    Wait-SampleLogContains "message flow" "SupportChat message-flow evidence"
    Write-Host "supportchat-server-evidence=completed"
    $RunSucceeded = $true
}
finally {
    Remove-SampleConfigurationFiles -RunDirectory $RunDir
    Stop-SampleProcesses
    if ($RedisContainer) {
        Remove-SampleRedisContainer $RedisContainer
    }
    if (-not $RunSucceeded -or $SUPPORTCHAT_KEEP_RUN_DIR -eq "1") {
        Write-Host "runDir=$RunDir"
    }
    else {
        Remove-Item -Recurse -Force $RunDir -ErrorAction SilentlyContinue
    }
}
