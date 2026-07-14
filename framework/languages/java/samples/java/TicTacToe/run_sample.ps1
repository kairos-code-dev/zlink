Set-StrictMode -Version Latest
. "$PSScriptRoot/../../redis-common.ps1"
$ErrorActionPreference = "Stop"

$SampleDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $SampleDir

$LogDir = Join-Path $SampleDir "build/sample-logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $LogDir "*.log")

$Gradle = if ($IsWindows) { Join-Path $SampleDir "../../gradlew.bat" } else { Join-Path $SampleDir "../../gradlew" }
$Processes = New-Object System.Collections.Generic.List[System.Diagnostics.Process]
$RedisContainer = $null

function Print-Logs {
    param([int]$Status)
    if ($Status -eq 0) { return }
    Get-ChildItem -Path $LogDir -Filter "*.log" -ErrorAction SilentlyContinue | ForEach-Object {
        Write-Error "===== $($_.FullName) ====="
        Get-Content -Path $_.FullName -Tail 200 -ErrorAction SilentlyContinue | ForEach-Object { Write-Error $_ }
    }
}

function Cleanup {
    param([int]$Status)
    Print-Logs $Status
    for ($i = $Processes.Count - 1; $i -ge 0; $i--) {
        $process = $Processes[$i]
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
    }
    if ($RedisContainer) {
        Remove-ZlinkSampleRedis $RedisContainer
    }
}

function Reserve-Ports {
    param([int]$Count)
    $listeners = New-Object System.Collections.Generic.List[System.Net.Sockets.TcpListener]
    $ports = New-Object System.Collections.Generic.List[int]
    try {
        while ($ports.Count -lt $Count) {
            $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Parse("127.0.0.1"), 0)
            $listener.Start()
            $listeners.Add($listener)
            $ports.Add($listener.LocalEndpoint.Port)
        }
        return $ports.ToArray()
    } finally {
        foreach ($listener in $listeners) {
            $listener.Stop()
        }
    }
}

function Wait-Port {
    param([int]$Port, [int]$TimeoutSeconds = 45)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $client = [System.Net.Sockets.TcpClient]::new()
        try {
            $connect = $client.BeginConnect("127.0.0.1", $Port, $null, $null)
            if ($connect.AsyncWaitHandle.WaitOne(200)) {
                $client.EndConnect($connect)
                return
            }
        } catch {
        } finally {
            $client.Close()
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for port $Port"
}

function Invoke-Gradle {
    param([string[]]$Arguments)
    & $Gradle @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Gradle failed: $($Arguments -join ' ')"
    }
}

function Start-SampleRole {
    param([string]$Role, [string]$ConfigPath, [string]$LogName)
    $logPath = Join-Path $LogDir $LogName
    $errorLogPath = Join-Path $LogDir ($LogName + ".err.log")
    $scriptName = if ($Role -eq "play") { "tictactoe-play" } else { "Server" }
    $serverBin = Join-Path $SampleDir "Server/build/install/Server/bin/$scriptName"
    if ($IsWindows) { $serverBin = "$serverBin.bat" }
    $process = Start-Process -FilePath $serverBin -ArgumentList @("--config", $ConfigPath) -WorkingDirectory $SampleDir -NoNewWindow -RedirectStandardOutput $logPath -RedirectStandardError $errorLogPath -PassThru
    $Processes.Add($process)
}

$Status = 1
try {
    $ports = Reserve-Ports 7
    $ApiPort = $ports[0]
    $ApiChannelPort = $ports[1]
    $PlayStreamPort = $ports[2]
    $PlayChannelPort = $ports[3]
    $SpotPort = $ports[4]
    $SpotPubPort = $ports[5]
    $PeerSpotPort = $ports[6]
    $redis = Start-ZlinkSampleRedis "zlink-redis-java-sample-tictactoe" "redis:7-alpine"
    $RedisContainer = $redis.ContainerId
    $RedisEndpoint = $redis.Endpoint
    $RedisKeyPrefix = "zlink:tictactoe:${PID}:$([Guid]::NewGuid().ToString('N')):room:"

    $ConfigFile = Join-Path $SampleDir "build/sample-application.properties"
    @(
        "sample.apiBindUrl=http://127.0.0.1:$ApiPort",
        "sample.apiPublicUrl=http://127.0.0.1:$ApiPort",
        "sample.apiChannelEndpoint=tcp://127.0.0.1:$ApiChannelPort",
        "sample.playChannelEndpoint=tcp://127.0.0.1:$PlayChannelPort",
        "sample.playChannelEndpoints=tcp://127.0.0.1:$PlayChannelPort",
        "sample.playEndpoint=tcp://127.0.0.1:$PlayStreamPort",
        "sample.playEndpoints=tcp://127.0.0.1:$PlayStreamPort",
        "sample.spotEndpoint=tcp://127.0.0.1:$SpotPort",
        "sample.spotEndpoints=tcp://127.0.0.1:$SpotPort",
        "sample.spotPubSubEndpoint=tcp://127.0.0.1:$SpotPubPort",
        "sample.spotPubSubEndpoints=tcp://127.0.0.1:$SpotPubPort",
        "sample.redisEndpoint=$RedisEndpoint",
        "sample.redisKeyPrefix=$RedisKeyPrefix",
        "sample.playSpotNodeRid=play-node-1",
        "sample.peerPlaySpotNodeRid=play-node-2",
        "sample.peerSpotEndpoint=tcp://127.0.0.1:$PeerSpotPort",
        "sample.peerSpotPubSubEndpoint=tcp://127.0.0.1:$PeerSpotPort",
        "sample.logDirectory=$LogDir"
    ) | Set-Content -Path $ConfigFile -Encoding UTF8

    Invoke-Gradle @("--settings-file", "standalone.settings.gradle.kts", ":Server:installDist", ":Client:installDist", "--quiet")

    Start-SampleRole "play" $ConfigFile "play.log"
    Wait-Port $PlayStreamPort
    Wait-Port $PlayChannelPort

    Start-SampleRole "api" $ConfigFile "api.log"
    Wait-Port $ApiPort
    Wait-Port $ApiChannelPort

    Invoke-Gradle @("--settings-file", "standalone.settings.gradle.kts", ":Client:run", "--quiet", "--args=--api-url http://127.0.0.1:$ApiPort")
    $Status = 0
} finally {
    Cleanup $Status
}
