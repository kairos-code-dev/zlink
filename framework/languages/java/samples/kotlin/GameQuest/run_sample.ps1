Set-StrictMode -Version Latest
. "$PSScriptRoot/../../redis-common.ps1"
$ErrorActionPreference = "Stop"

$SampleDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $SampleDir

$LogDir = Join-Path $SampleDir "build/sample-logs"
$SampleFlowLogDir = Join-Path $SampleDir "logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
New-Item -ItemType Directory -Force -Path $SampleFlowLogDir | Out-Null
Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $LogDir "*.log")
Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $SampleFlowLogDir "*.log")

$Gradle = if ($IsWindows) { Join-Path $SampleDir "../../gradlew.bat" } else { Join-Path $SampleDir "../../gradlew" }
$Processes = New-Object System.Collections.Generic.List[System.Diagnostics.Process]
$RedisContainerId = $null

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
    if ($RedisContainerId) {
        Remove-ZlinkSampleRedis $RedisContainerId
    }
}

function Reserve-Endpoints {
    param([int]$Count)
    $listeners = New-Object System.Collections.Generic.List[System.Net.Sockets.TcpListener]
    $endpoints = New-Object System.Collections.Generic.List[string]
    try {
        while ($endpoints.Count -lt $Count) {
            $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Parse("127.0.0.1"), 0)
            $listener.Start()
            $listeners.Add($listener)
            $endpoints.Add("127.0.0.1:$($listener.LocalEndpoint.Port)")
        }
        return $endpoints.ToArray()
    } finally {
        foreach ($listener in $listeners) {
            $listener.Stop()
        }
    }
}

function Wait-Port {
    param([string]$HostName, [int]$Port, [int]$TimeoutSeconds = 60)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $client = [System.Net.Sockets.TcpClient]::new()
        try {
            $connect = $client.BeginConnect($HostName, $Port, $null, $null)
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
    throw "Timed out waiting for ${HostName}:$Port"
}

function Wait-HttpHealth {
    param([string]$BaseUrl, [int]$TimeoutSeconds = 60)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            Invoke-WebRequest -Method Get -Uri "$BaseUrl/health" -UseBasicParsing -TimeoutSec 2 | Out-Null
            return
        } catch {
            Start-Sleep -Milliseconds 100
        }
    }
    throw "Timed out waiting for health at $BaseUrl"
}

function Split-Endpoint {
    param([string]$Endpoint)
    $parts = $Endpoint.Split(":")
    return @{ Host = $parts[0]; Port = [int]$parts[1] }
}

function Invoke-Gradle {
    param([string[]]$Arguments)
    & $Gradle "--no-parallel" "--max-workers=1" @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Gradle failed: $($Arguments -join ' ')"
    }
}

function Start-Role {
    param([string]$Project, [string]$ScriptName, [string]$LogName, [string]$JavaOptions)
    $bin = Join-Path $SampleDir "$Project/build/install/$ScriptName/bin/$ScriptName"
    if ($IsWindows) {
        $bin = "$bin.bat"
    }
    $logPath = Join-Path $LogDir $LogName
    $errorLogPath = Join-Path $LogDir ($LogName + ".err.log")
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $bin
    $startInfo.WorkingDirectory = $SampleDir
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.UseShellExecute = $false
    $startInfo.Environment["JAVA_TOOL_OPTIONS"] = $JavaOptions
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    $process.Start() | Out-Null
    $process.BeginOutputReadLine()
    $process.BeginErrorReadLine()
    Register-ObjectEvent -InputObject $process -EventName OutputDataReceived -Action {
        if ($EventArgs.Data) { Add-Content -Path $Event.MessageData.Out -Value $EventArgs.Data }
    } -MessageData @{ Out = $logPath } | Out-Null
    Register-ObjectEvent -InputObject $process -EventName ErrorDataReceived -Action {
        if ($EventArgs.Data) { Add-Content -Path $Event.MessageData.Err -Value $EventArgs.Data }
    } -MessageData @{ Err = $errorLogPath } | Out-Null
    $Processes.Add($process)
}

$Status = 1
$oldJavaToolOptions = $env:JAVA_TOOL_OPTIONS
try {
    $endpoints = Reserve-Endpoints 8
    $apiAStream = Split-Endpoint $endpoints[0]
    $apiBStream = Split-Endpoint $endpoints[1]
    $apiAHttp = Split-Endpoint $endpoints[2]
    $apiBHttp = Split-Endpoint $endpoints[3]
    $missionARoute = Split-Endpoint $endpoints[4]
    $missionBRoute = Split-Endpoint $endpoints[5]
    $missionAHttp = Split-Endpoint $endpoints[6]
    $missionBHttp = Split-Endpoint $endpoints[7]

    $redisInstance = Start-ZlinkSampleRedis "zlink-redis-kotlin-sample-gamequest"
    $RedisContainerId = $redisInstance.ContainerId
    $redisEndpoint = $redisInstance.Endpoint
    $redis = Split-Endpoint $redisEndpoint
    Wait-Port $redis.Host $redis.Port

    $redisKeyPrefix = if ($env:GAMEQUEST_REDIS_KEY_PREFIX) { $env:GAMEQUEST_REDIS_KEY_PREFIX } else { "gamequest:kotlin:${PID}:$([Guid]::NewGuid().ToString('N')):" }

    $prefix = "zlink.samples.gamequest"
    $commonJavaOptions = "$oldJavaToolOptions -D$prefix.apiAStreamEndpoint=tcp://$($apiAStream.Host):$($apiAStream.Port) -D$prefix.apiBStreamEndpoint=tcp://$($apiBStream.Host):$($apiBStream.Port) -D$prefix.apiAHttpEndpoint=http://$($apiAHttp.Host):$($apiAHttp.Port) -D$prefix.apiBHttpEndpoint=http://$($apiBHttp.Host):$($apiBHttp.Port) -D$prefix.missionARouteEndpoint=tcp://$($missionARoute.Host):$($missionARoute.Port) -D$prefix.missionBRouteEndpoint=tcp://$($missionBRoute.Host):$($missionBRoute.Port) -D$prefix.missionAHttpEndpoint=http://$($missionAHttp.Host):$($missionAHttp.Port) -D$prefix.missionBHttpEndpoint=http://$($missionBHttp.Host):$($missionBHttp.Port) -D$prefix.redisEndpoint=$redisEndpoint -D$prefix.redisKeyPrefix=$redisKeyPrefix"

    Push-Location "../../.."
    try {
        & ./gradlew --no-daemon --no-parallel --max-workers=1 :zlink-framework-core:jar :zlink-framework-kotlin:jar :zlink-framework-spring-boot-starter:jar :zlink-framework-locations-redis:jar :zlink-stream-connector:jar --quiet
        if ($LASTEXITCODE -ne 0) { throw "Framework jar build failed" }
    } finally {
        Pop-Location
    }

    Invoke-Gradle @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", ":Server:GameApi:installDist", ":Server:QuestMission:installDist", ":Client:installDist", "--quiet")

    Start-Role -Project "Server/QuestMission" -ScriptName "QuestMission" -LogName "mission-a.log" -JavaOptions "$commonJavaOptions -Dzlink.samples.gamequest.missionName=mission-a"
    Start-Role -Project "Server/QuestMission" -ScriptName "QuestMission" -LogName "mission-b.log" -JavaOptions "$commonJavaOptions -Dzlink.samples.gamequest.missionName=mission-b"
    Wait-Port $missionARoute.Host $missionARoute.Port
    Wait-Port $missionBRoute.Host $missionBRoute.Port
    Wait-HttpHealth "http://$($missionAHttp.Host):$($missionAHttp.Port)"
    Wait-HttpHealth "http://$($missionBHttp.Host):$($missionBHttp.Port)"

    Start-Role -Project "Server/GameApi" -ScriptName "GameApi" -LogName "api-a.log" -JavaOptions "$commonJavaOptions -Dzlink.samples.gamequest.apiName=api-a"
    Start-Role -Project "Server/GameApi" -ScriptName "GameApi" -LogName "api-b.log" -JavaOptions "$commonJavaOptions -Dzlink.samples.gamequest.apiName=api-b"
    Wait-Port $apiAStream.Host $apiAStream.Port
    Wait-Port $apiBStream.Host $apiBStream.Port
    Wait-HttpHealth "http://$($apiAHttp.Host):$($apiAHttp.Port)"
    Wait-HttpHealth "http://$($apiBHttp.Host):$($apiBHttp.Port)"

    Write-Host "topology=ready"
    $clientBin = Join-Path $SampleDir "Client/build/install/Client/bin/Client"
    if ($IsWindows) {
        $clientBin = "$clientBin.bat"
    }
    $env:JAVA_TOOL_OPTIONS = $commonJavaOptions
    & $clientBin | Tee-Object -FilePath (Join-Path $LogDir "client.log")
    if ($LASTEXITCODE -ne 0) { throw "Client failed" }

    Select-String -Path (Join-Path $LogDir "client.log") -Pattern "gamequest-server-evidence=completed" | Out-Null
    Select-String -Path (Join-Path $LogDir "client.log") -Pattern "gamequest=completed" | Out-Null
    Write-Host "gamequest kotlin full client/server self-check completed"
    $Status = 0
} finally {
    Cleanup $Status
    $env:JAVA_TOOL_OPTIONS = $oldJavaToolOptions
}
