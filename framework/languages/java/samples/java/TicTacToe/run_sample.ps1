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

function Start-GradleRole {
    param([string[]]$Arguments, [string]$LogName)
    $logPath = Join-Path $LogDir $LogName
    $errorLogPath = Join-Path $LogDir ($LogName + ".err.log")
    $quotedArguments = $Arguments | ForEach-Object {
        if ($_ -match '\s') {
            '"' + ($_ -replace '"', '\"') + '"'
        } else {
            $_
        }
    }
    $process = Start-Process -FilePath $Gradle -ArgumentList $quotedArguments -WorkingDirectory $SampleDir -NoNewWindow -RedirectStandardOutput $logPath -RedirectStandardError $errorLogPath -PassThru
    $Processes.Add($process)
}

$Status = 1
try {
    $ports = Reserve-Ports 5
    $ApiPort = $ports[0]
    $ApiChannelPort = $ports[1]
    $PlayStreamPort = $ports[2]
    $PlayChannelPort = $ports[3]
    $SpotPort = $ports[4]
    $redis = Start-ZlinkSampleRedis "zlink-redis-java-sample-tictactoe" "redis:7-alpine"
    $RedisContainer = $redis.ContainerId
    $RedisEndpoint = $redis.Endpoint
    $RedisKeyPrefix = if ($env:TICTACTOE_REDIS_KEY_PREFIX) {
        $env:TICTACTOE_REDIS_KEY_PREFIX
    } else {
        "zlink:tictactoe:${PID}:$([Guid]::NewGuid().ToString('N')):room:"
    }

    $ConfigFile = Join-Path $SampleDir "build/sample-application.properties"
    @(
        "sample.apiBindUrl=http://127.0.0.1:$ApiPort",
        "sample.apiPublicUrl=http://127.0.0.1:$ApiPort",
        "sample.apiChannelEndpoint=tcp://127.0.0.1:$ApiChannelPort",
        "sample.playChannelEndpoint=tcp://127.0.0.1:$PlayChannelPort",
        "sample.playEndpoint=tcp://127.0.0.1:$PlayStreamPort",
        "sample.spotEndpoint=tcp://127.0.0.1:$SpotPort",
        "sample.redisEndpoint=$RedisEndpoint",
        "sample.redisKeyPrefix=$RedisKeyPrefix",
        "sample.logDirectory=$LogDir"
    ) | Set-Content -Path $ConfigFile -Encoding UTF8

    Start-GradleRole -Arguments @("--settings-file", "standalone.settings.gradle.kts", ":Server:run", "--quiet", "--args=play --config $ConfigFile") -LogName "play.log"
    Wait-Port $PlayStreamPort
    Wait-Port $PlayChannelPort

    Start-GradleRole -Arguments @("--settings-file", "standalone.settings.gradle.kts", ":Server:run", "--quiet", "--args=api --config $ConfigFile") -LogName "api.log"
    Wait-Port $ApiPort
    Wait-Port $ApiChannelPort

    Invoke-Gradle @("--settings-file", "standalone.settings.gradle.kts", ":Client:run", "--quiet", "--args=--api-url http://127.0.0.1:$ApiPort")
    $Status = 0
} finally {
    Cleanup $Status
}
