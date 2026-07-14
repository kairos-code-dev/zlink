$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "../sample_runner.ps1")

$RunDir = New-SampleRunDirectory "tictactoe-dotnet"
$RunId = "$PID-$([Guid]::NewGuid().ToString('N'))"
$LogDir = Join-Path $RunDir "logs"
$SampleLogDir = Join-Path $RunDir "sample-logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
New-Item -ItemType Directory -Force -Path $SampleLogDir | Out-Null
$env:TICTACTOE_LOG_DIR = $SampleLogDir
$redisContainerId = $null
$RunSucceeded = $false

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
    $env:TICTACTOE_REDIS_KEY_PREFIX = "tictactoe:dotnet:${RunId}:"

    $ports = New-SamplePorts -Count 13 -BasePort 0

    $apiABindUrl = "http://127.0.0.1:$($ports[0])"
    $apiBBindUrl = "http://127.0.0.1:$($ports[1])"
    $apiAPublicUrl = $apiABindUrl
    $apiBPublicUrl = $apiBBindUrl
    $apiAChannelEndpoint = "tcp://127.0.0.1:$($ports[2])"
    $apiBChannelEndpoint = "tcp://127.0.0.1:$($ports[3])"
    $playAChannelEndpoint = "tcp://127.0.0.1:$($ports[4])"
    $playBChannelEndpoint = "tcp://127.0.0.1:$($ports[5])"
    $playAEndpoint = "tcp://127.0.0.1:$($ports[6])"
    $playBEndpoint = "tcp://127.0.0.1:$($ports[7])"
    $spotAEndpoint = "tcp://127.0.0.1:$($ports[8])"
    $spotBEndpoint = "tcp://127.0.0.1:$($ports[9])"
    $spotAPubSubEndpoint = "tcp://127.0.0.1:$($ports[10])"
    $spotBPubSubEndpoint = "tcp://127.0.0.1:$($ports[11])"
    $apiAConfigFile = Join-Path $RunDir "appsettings.api-a.json"
    $apiBConfigFile = Join-Path $RunDir "appsettings.api-b.json"
    $playAConfigFile = Join-Path $RunDir "appsettings.play-a.json"
    $playBConfigFile = Join-Path $RunDir "appsettings.play-b.json"

    $redis = Start-SampleRedisContainer "zlink-tictactoe-dotnet-redis"
    $redisContainerId = $redis.ContainerId
    $env:TICTACTOE_REDIS_ENDPOINT = $redis.Endpoint
    $redisEndpoint = $env:TICTACTOE_REDIS_ENDPOINT

    function New-TicTacToeSettings {
        param(
            [string]$InstanceName,
            [int]$ApiIndex,
            [int]$PlayIndex,
            [int]$PeerPlayIndex
        )

        @{
            Sample = @{
                InstanceName = $InstanceName
                ApiIndex = $ApiIndex
                PlayIndex = $PlayIndex
                ApiBindUrls = @($apiABindUrl, $apiBBindUrl)
                ApiPublicUrls = @($apiAPublicUrl, $apiBPublicUrl)
                ApiChannelEndpoints = @($apiAChannelEndpoint, $apiBChannelEndpoint)
                PlayChannelEndpoints = @($playAChannelEndpoint, $playBChannelEndpoint)
                PlayEndpoints = @($playAEndpoint, $playBEndpoint)
                SpotEndpoints = @($spotAEndpoint, $spotBEndpoint)
                SpotPubSubEndpoints = @($spotAPubSubEndpoint, $spotBPubSubEndpoint)
                PlaySpotNodeRid = "play-node-$($PlayIndex + 1)"
                PeerPlaySpotNodeRid = "play-node-$($PeerPlayIndex + 1)"
                PeerSpotEndpoint = @($spotAEndpoint, $spotBEndpoint)[$PeerPlayIndex]
                PeerSpotPubEndpoint = @($spotAPubSubEndpoint, $spotBPubSubEndpoint)[$PeerPlayIndex]
                RedisEndpoint = $redisEndpoint
                RedisKeyPrefix = $env:TICTACTOE_REDIS_KEY_PREFIX
                LogDirectory = $LogDir
            }
        }
    }
    New-TicTacToeSettings "api-a" 0 0 1 | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 -Path $apiAConfigFile
    New-TicTacToeSettings "api-b" 1 0 1 | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 -Path $apiBConfigFile
    New-TicTacToeSettings "play-a" 0 0 1 | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 -Path $playAConfigFile
    New-TicTacToeSettings "play-b" 0 1 0 | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 -Path $playBConfigFile

    Invoke-SampleDotnetBuild (Join-Path $ScriptDir "TicTacToe.sln")

    Wait-SampleTcpEndpoint "redis" "tcp://$redisEndpoint"

    Start-SampleDotnetAssembly -Name "play-a" -Project (Join-Path $ScriptDir "Server.Play/TicTacToe.Server.Play.csproj") -LogDirectory $LogDir -Arguments @("--config", $playAConfigFile) | Out-Null
    Wait-SampleTcpEndpoint "play-a-stream" $playAEndpoint
    Wait-SampleTcpEndpoint "play-a-channel" $playAChannelEndpoint
    Wait-SampleTcpEndpoint "play-a-spot" $spotAEndpoint
    Wait-SampleTcpEndpoint "play-a-spot-pubsub" $spotAPubSubEndpoint

    Start-SampleDotnetAssembly -Name "play-b" -Project (Join-Path $ScriptDir "Server.Play/TicTacToe.Server.Play.csproj") -LogDirectory $LogDir -Arguments @("--config", $playBConfigFile) | Out-Null
    Wait-SampleTcpEndpoint "play-b-stream" $playBEndpoint
    Wait-SampleTcpEndpoint "play-b-channel" $playBChannelEndpoint
    Wait-SampleTcpEndpoint "play-b-spot" $spotBEndpoint
    Wait-SampleTcpEndpoint "play-b-spot-pubsub" $spotBPubSubEndpoint

    Start-SampleDotnetAssembly -Name "api-a" -Project (Join-Path $ScriptDir "Server.Api/TicTacToe.Server.Api.csproj") -LogDirectory $LogDir -Arguments @("--config", $apiAConfigFile) | Out-Null
    Wait-SampleTcpEndpoint "api-a-http" $apiABindUrl
    Wait-SampleTcpEndpoint "api-a-channel" $apiAChannelEndpoint

    Start-SampleDotnetAssembly -Name "api-b" -Project (Join-Path $ScriptDir "Server.Api/TicTacToe.Server.Api.csproj") -LogDirectory $LogDir -Arguments @("--config", $apiBConfigFile) | Out-Null
    Wait-SampleTcpEndpoint "api-b-http" $apiBBindUrl
    Wait-SampleTcpEndpoint "api-b-channel" $apiBChannelEndpoint

    $clientLog = Join-Path $LogDir "client.log"
    Invoke-SampleDotnetRun -Project (Join-Path $ScriptDir "Client/TicTacToe.Client.csproj") -Arguments @("--api-url", $apiAPublicUrl, "--log-dir", $SampleLogDir) *> $clientLog
    Wait-LogContains $clientLog "stream-inbound sample=TicTacToe" "TicTacToe stream-inbound marker"
    Wait-LogContains $clientLog "stream-inbound sample=TicTacToe .* seq=[0-9]" "TicTacToe sequenced stream-inbound response marker"
    Wait-LogContains $clientLog "stream-inbound sample=TicTacToe .* name=.*Notify" "TicTacToe stream-inbound push marker"
    Wait-LogContains $clientLog "observer-win-milestone=verified" "TicTacToe observer win milestone notification"
    $playLogs = Join-Path $LogDir "play-*.log"
    Wait-LogContains $playLogs "actor: LeaveGameReq completed. actor=player-x" "TicTacToe player-x LeaveGameReq completion"
    Wait-LogContains $playLogs "actor: LeaveGameReq completed. actor=player-o" "TicTacToe player-o LeaveGameReq completion"
    Wait-LogContains $playLogs "entry spot: actor destroy completed. actor=player-x" "TicTacToe player-x destroy completion"
    Wait-LogContains $playLogs "entry spot: actor destroy completed. actor=player-o" "TicTacToe player-o destroy completion"
    $dispatchError = Get-ChildItem -Path $LogDir -Filter "*.log" |
        Select-String -Pattern "dispatch-error" -List |
        Select-Object -First 1
    if ($null -ne $dispatchError) {
        throw "Unexpected dispatch-error in TicTacToe sample logs."
    }
    $messageFlowError = Get-ChildItem -Path $SampleLogDir -Filter "*.log" |
        Select-String -Pattern "message flow outcome=error" -List |
        Select-Object -First 1
    if ($null -ne $messageFlowError) {
        throw "Unexpected message-flow error in TicTacToe sample logs."
    }
    Wait-SampleLogContains "message flow" "TicTacToe message-flow evidence"
    $RunSucceeded = $true
}
finally {
    Stop-SampleProcesses
    if ($redisContainerId) {
        Remove-SampleRedisContainer $redisContainerId
    }
    if (-not $RunSucceeded -or $env:TICTACTOE_KEEP_RUN_DIR -eq "1") {
        Write-Host "runDir=$RunDir"
    }
    else {
        Remove-Item -Recurse -Force $RunDir -ErrorAction SilentlyContinue
    }
}
