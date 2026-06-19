$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "../sample_runner.ps1")

$RunDir = New-SampleRunDirectory "tictactoe-dotnet"
$LogDir = Join-Path $RunDir "logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$redisContainerId = $null

try {
    $basePort = if ($env:TICTACTOE_BASE_PORT) { [int]$env:TICTACTOE_BASE_PORT } else { 0 }
    $ports = New-SamplePorts -Count 13 -BasePort $basePort

    $apiABindUrl = if ($env:TICTACTOE_API_A_BIND_URL) { $env:TICTACTOE_API_A_BIND_URL } else { "http://127.0.0.1:$($ports[0])" }
    $apiBBindUrl = if ($env:TICTACTOE_API_B_BIND_URL) { $env:TICTACTOE_API_B_BIND_URL } else { "http://127.0.0.1:$($ports[1])" }
    $apiAPublicUrl = if ($env:TICTACTOE_API_A_PUBLIC_URL) { $env:TICTACTOE_API_A_PUBLIC_URL } else { $apiABindUrl }
    $apiBPublicUrl = if ($env:TICTACTOE_API_B_PUBLIC_URL) { $env:TICTACTOE_API_B_PUBLIC_URL } else { $apiBBindUrl }
    $apiAChannelEndpoint = if ($env:TICTACTOE_API_A_CHANNEL_ENDPOINT) { $env:TICTACTOE_API_A_CHANNEL_ENDPOINT } else { "tcp://127.0.0.1:$($ports[2])" }
    $apiBChannelEndpoint = if ($env:TICTACTOE_API_B_CHANNEL_ENDPOINT) { $env:TICTACTOE_API_B_CHANNEL_ENDPOINT } else { "tcp://127.0.0.1:$($ports[3])" }
    $playAChannelEndpoint = if ($env:TICTACTOE_PLAY_A_CHANNEL_ENDPOINT) { $env:TICTACTOE_PLAY_A_CHANNEL_ENDPOINT } else { "tcp://127.0.0.1:$($ports[4])" }
    $playBChannelEndpoint = if ($env:TICTACTOE_PLAY_B_CHANNEL_ENDPOINT) { $env:TICTACTOE_PLAY_B_CHANNEL_ENDPOINT } else { "tcp://127.0.0.1:$($ports[5])" }
    $playAEndpoint = if ($env:TICTACTOE_PLAY_A_ENDPOINT) { $env:TICTACTOE_PLAY_A_ENDPOINT } else { "tcp://127.0.0.1:$($ports[6])" }
    $playBEndpoint = if ($env:TICTACTOE_PLAY_B_ENDPOINT) { $env:TICTACTOE_PLAY_B_ENDPOINT } else { "tcp://127.0.0.1:$($ports[7])" }
    $spotAEndpoint = if ($env:TICTACTOE_SPOT_A_ENDPOINT) { $env:TICTACTOE_SPOT_A_ENDPOINT } else { "tcp://127.0.0.1:$($ports[8])" }
    $spotBEndpoint = if ($env:TICTACTOE_SPOT_B_ENDPOINT) { $env:TICTACTOE_SPOT_B_ENDPOINT } else { "tcp://127.0.0.1:$($ports[9])" }
    $spotAPubSubEndpoint = if ($env:TICTACTOE_SPOT_A_PUBSUB_ENDPOINT) { $env:TICTACTOE_SPOT_A_PUBSUB_ENDPOINT } else { "tcp://127.0.0.1:$($ports[10])" }
    $spotBPubSubEndpoint = if ($env:TICTACTOE_SPOT_B_PUBSUB_ENDPOINT) { $env:TICTACTOE_SPOT_B_PUBSUB_ENDPOINT } else { "tcp://127.0.0.1:$($ports[11])" }
    $redisEndpoint = if ($env:TICTACTOE_REDIS_ENDPOINT) { $env:TICTACTOE_REDIS_ENDPOINT } else { "127.0.0.1:$($ports[12])" }
    $apiAConfigFile = Join-Path $RunDir "appsettings.api-a.json"
    $apiBConfigFile = Join-Path $RunDir "appsettings.api-b.json"
    $playAConfigFile = Join-Path $RunDir "appsettings.play-a.json"
    $playBConfigFile = Join-Path $RunDir "appsettings.play-b.json"

    if (-not $env:TICTACTOE_REDIS_ENDPOINT) {
        if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
            throw "TICTACTOE_REDIS_ENDPOINT is not set and docker is not available."
        }
        $redisContainerId = (& docker run -d --rm -p "127.0.0.1:$($ports[12]):6379" redis:7-alpine).Trim()
        if ($LASTEXITCODE -ne 0) {
            throw "docker failed to start Redis."
        }
    }

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
                PeerSpotPubSubEndpoint = @($spotAPubSubEndpoint, $spotBPubSubEndpoint)[$PeerPlayIndex]
                RedisEndpoint = $redisEndpoint
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

    Start-SampleDotnetAssembly -Name "play-a" -Project (Join-Path $ScriptDir "Server/TicTacToe.Server.csproj") -LogDirectory $LogDir -Arguments @("play-a", "--config", $playAConfigFile) | Out-Null
    Wait-SampleTcpEndpoint "play-a-stream" $playAEndpoint
    Wait-SampleTcpEndpoint "play-a-channel" $playAChannelEndpoint
    Wait-SampleTcpEndpoint "play-a-spot" $spotAEndpoint
    Wait-SampleTcpEndpoint "play-a-spot-pubsub" $spotAPubSubEndpoint

    Start-SampleDotnetAssembly -Name "play-b" -Project (Join-Path $ScriptDir "Server/TicTacToe.Server.csproj") -LogDirectory $LogDir -Arguments @("play-b", "--config", $playBConfigFile) | Out-Null
    Wait-SampleTcpEndpoint "play-b-stream" $playBEndpoint
    Wait-SampleTcpEndpoint "play-b-channel" $playBChannelEndpoint
    Wait-SampleTcpEndpoint "play-b-spot" $spotBEndpoint
    Wait-SampleTcpEndpoint "play-b-spot-pubsub" $spotBPubSubEndpoint
    Start-Sleep -Seconds 2

    Start-SampleDotnetAssembly -Name "api-a" -Project (Join-Path $ScriptDir "Server/TicTacToe.Server.csproj") -LogDirectory $LogDir -Arguments @("api-a", "--config", $apiAConfigFile) | Out-Null
    Wait-SampleTcpEndpoint "api-a-http" $apiABindUrl
    Wait-SampleTcpEndpoint "api-a-channel" $apiAChannelEndpoint

    Start-SampleDotnetAssembly -Name "api-b" -Project (Join-Path $ScriptDir "Server/TicTacToe.Server.csproj") -LogDirectory $LogDir -Arguments @("api-b", "--config", $apiBConfigFile) | Out-Null
    Wait-SampleTcpEndpoint "api-b-http" $apiBBindUrl
    Wait-SampleTcpEndpoint "api-b-channel" $apiBChannelEndpoint

    $clientLog = Join-Path $LogDir "client.log"
    Invoke-SampleDotnetRun -Project (Join-Path $ScriptDir "Client/TicTacToe.Client.csproj") -Arguments @("--api-url", $apiAPublicUrl) *> $clientLog
    if (-not (Select-String -Path $clientLog -Pattern "stream-inbound sample=TicTacToe" -Quiet)) {
        throw "TicTacToe client did not write stream-inbound marker."
    }
    if (-not (Select-String -Path $clientLog -Pattern "stream-inbound sample=TicTacToe .* seq=[0-9]" -Quiet)) {
        throw "TicTacToe client did not write sequenced stream-inbound response marker."
    }
    if (-not (Select-String -Path $clientLog -Pattern "stream-inbound sample=TicTacToe .* name=.*Notify" -Quiet)) {
        throw "TicTacToe client did not write stream-inbound push marker."
    }
    if (-not (Select-String -Path $clientLog -Pattern "observer-win-milestone=verified" -Quiet)) {
        throw "TicTacToe client did not verify observer win milestone notification."
    }
    $playLogs = Join-Path $LogDir "play-*.log"
    if (-not (Select-String -Path $playLogs -Pattern "actor: LeaveGameReq completed. actor=player-x" -Quiet)) {
        throw "TicTacToe play log did not include player-x LeaveGameReq completion."
    }
    if (-not (Select-String -Path $playLogs -Pattern "actor: LeaveGameReq completed. actor=player-o" -Quiet)) {
        throw "TicTacToe play log did not include player-o LeaveGameReq completion."
    }
    if (-not (Select-String -Path $playLogs -Pattern "entry spot: actor destroy completed. actor=player-x" -Quiet)) {
        throw "TicTacToe play log did not include player-x destroy completion."
    }
    if (-not (Select-String -Path $playLogs -Pattern "entry spot: actor destroy completed. actor=player-o" -Quiet)) {
        throw "TicTacToe play log did not include player-o destroy completion."
    }
}
finally {
    Stop-SampleProcesses
    if ($redisContainerId) {
        & docker rm -f $redisContainerId *> $null
    }
    if ($env:TICTACTOE_KEEP_RUN_DIR -eq "1") {
        Write-Host "runDir=$RunDir"
    }
    else {
        Remove-Item -Recurse -Force $RunDir -ErrorAction SilentlyContinue
    }
}
