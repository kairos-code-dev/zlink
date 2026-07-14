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
$env:SUPPORTCHAT_LOG_DIR = $SampleLogDir

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

    $env:SUPPORTCHAT_API_CHANNEL_ENDPOINT = "tcp://127.0.0.1:$($ports[0])"
    $env:SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT = "tcp://127.0.0.1:$($ports[1])"
    $env:SUPPORTCHAT_SESSION_SPOT_ENDPOINT = "tcp://127.0.0.1:$($ports[2])"
    $env:SUPPORTCHAT_SESSION_ROUTER_ENDPOINT = "tcp://127.0.0.1:$($ports[3])"
    $env:SUPPORTCHAT_ENTRY_SPOT_ENDPOINT = "tcp://127.0.0.1:$($ports[4])"
    $env:SUPPORTCHAT_ENTRY_SPOT_ROUTER_ENDPOINT = "tcp://127.0.0.1:$($ports[5])"
    $env:SUPPORTCHAT_STREAM_ENDPOINT = "tcp://127.0.0.1:$($ports[6])"
    $env:SUPPORTCHAT_REDIS_KEY_PREFIX = "supportchat:dotnet:${RunId}:"

    $redis = Start-SampleRedisContainer "zlink-supportchat-dotnet-redis"
    $RedisContainer = $redis.ContainerId
    $env:SUPPORTCHAT_REDIS_ENDPOINT = $redis.Endpoint
    Wait-SampleTcpEndpoint "redis" "tcp://$env:SUPPORTCHAT_REDIS_ENDPOINT"
    $configFile = Join-Path $RunDir "appsettings.json"
    @{
        Sample = [ordered]@{
            LogDirectory = $SampleLogDir
            RedisEndpoint = $env:SUPPORTCHAT_REDIS_ENDPOINT
            RedisKeyPrefix = $env:SUPPORTCHAT_REDIS_KEY_PREFIX
            ApiChannelEndpoint = $env:SUPPORTCHAT_API_CHANNEL_ENDPOINT
            SupportChannelEndpoint = $env:SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT
            SessionSpotEndpoint = $env:SUPPORTCHAT_SESSION_SPOT_ENDPOINT
            SessionRouterEndpoint = $env:SUPPORTCHAT_SESSION_ROUTER_ENDPOINT
            SupportEntrySpotEndpoint = $env:SUPPORTCHAT_ENTRY_SPOT_ENDPOINT
            SupportEntrySpotRouterEndpoint = $env:SUPPORTCHAT_ENTRY_SPOT_ROUTER_ENDPOINT
            StreamEndpoint = $env:SUPPORTCHAT_STREAM_ENDPOINT
        }
    } | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 -Path $configFile

    Invoke-SampleDotnetBuild (Join-Path $ScriptDir "SupportChat.csproj")

    Start-SampleDotnetAssembly -Name "support" -Project (Join-Path $ScriptDir "Server/Support/SupportChat.Server.Support.csproj") -LogDirectory $LogDir -Arguments @("--config", $configFile) | Out-Null
    Wait-SampleTcpEndpoint "support-channel" $env:SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT
    Wait-SampleTcpEndpoint "support-spot-router" $env:SUPPORTCHAT_ENTRY_SPOT_ROUTER_ENDPOINT
    Wait-SampleTcpEndpoint "support-spot-pub" $env:SUPPORTCHAT_ENTRY_SPOT_ENDPOINT

    Start-SampleDotnetAssembly -Name "api" -Project (Join-Path $ScriptDir "Server/Api/SupportChat.Server.Api.csproj") -LogDirectory $LogDir -Arguments @("--config", $configFile) | Out-Null
    Wait-SampleTcpEndpoint "api" $env:SUPPORTCHAT_API_CHANNEL_ENDPOINT

    Start-SampleDotnetAssembly -Name "session" -Project (Join-Path $ScriptDir "Server/Session/SupportChat.Server.Session.csproj") -LogDirectory $LogDir -Arguments @("--config", $configFile) | Out-Null
    Wait-SampleTcpEndpoint "session-route" $env:SUPPORTCHAT_SESSION_SPOT_ENDPOINT
    Wait-SampleTcpEndpoint "session-router" $env:SUPPORTCHAT_SESSION_ROUTER_ENDPOINT
    Wait-SampleTcpEndpoint "session-stream" $env:SUPPORTCHAT_STREAM_ENDPOINT

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
    Stop-SampleProcesses
    if ($RedisContainer) {
        Remove-SampleRedisContainer $RedisContainer
    }
    if (-not $RunSucceeded -or $env:SUPPORTCHAT_KEEP_RUN_DIR -eq "1") {
        Write-Host "runDir=$RunDir"
    }
    else {
        Remove-Item -Recurse -Force $RunDir -ErrorAction SilentlyContinue
    }
}
