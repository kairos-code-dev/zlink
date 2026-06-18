Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$SampleDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $SampleDir

$LogDir = Join-Path $SampleDir "build/sample-logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $LogDir "*.log")

$Gradle = if ($IsWindows) { Join-Path $SampleDir "../../gradlew.bat" } else { Join-Path $SampleDir "../../gradlew" }
$RolePattern = "systems\.zlink\.samples\.bingo\.(server\.(registry|api|play|session)\.Program|client\.Program|probe\.Program)"
$Processes = New-Object System.Collections.Generic.List[System.Diagnostics.Process]

function Print-Logs {
    param([int]$Status)
    if ($Status -eq 0) { return }
    Get-ChildItem -Path $LogDir -Filter "*.log" -ErrorAction SilentlyContinue | ForEach-Object {
        Write-Error "===== $($_.FullName) ====="
        Get-Content -Path $_.FullName -Tail 200 -ErrorAction SilentlyContinue | ForEach-Object { Write-Error $_ }
    }
}

function Stop-RoleProcesses {
    if ($IsWindows) {
        Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match $RolePattern } | ForEach-Object {
            Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
        }
    } else {
        & pgrep -f $RolePattern 2>$null | ForEach-Object {
            & kill -9 $_ 2>$null
        }
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
    Stop-RoleProcesses
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

function Split-Endpoint {
    param([string]$Endpoint)
    $parts = $Endpoint.Split(":")
    return @{ Host = $parts[0]; Port = [int]$parts[1] }
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
    $process = Start-Process -FilePath $Gradle -ArgumentList $Arguments -WorkingDirectory $SampleDir -NoNewWindow -RedirectStandardOutput $logPath -RedirectStandardError $errorLogPath -PassThru
    $Processes.Add($process)
}

$Status = 1
$oldJavaToolOptions = $env:JAVA_TOOL_OPTIONS
try {
    $endpoints = Reserve-Endpoints 11
    $registryPub = Split-Endpoint $endpoints[0]
    $registryRouter = Split-Endpoint $endpoints[1]
    $apiChannel = Split-Endpoint $endpoints[2]
    $playChannel = Split-Endpoint $endpoints[3]
    $sessionSpot = Split-Endpoint $endpoints[4]
    $sessionRouter = Split-Endpoint $endpoints[5]
    $playSpot = Split-Endpoint $endpoints[6]
    $playRouter = Split-Endpoint $endpoints[7]
    $sessionRoute = Split-Endpoint $endpoints[8]
    $playRoute = Split-Endpoint $endpoints[9]
    $stream = Split-Endpoint $endpoints[10]

    $env:JAVA_TOOL_OPTIONS = "$oldJavaToolOptions -Dzlink.samples.bingo.registryPubEndpoint=tcp://$($registryPub.Host):$($registryPub.Port) -Dzlink.samples.bingo.registryRouterEndpoint=tcp://$($registryRouter.Host):$($registryRouter.Port) -Dzlink.samples.bingo.apiChannelEndpoint=tcp://$($apiChannel.Host):$($apiChannel.Port) -Dzlink.samples.bingo.playChannelEndpoint=tcp://$($playChannel.Host):$($playChannel.Port) -Dzlink.samples.bingo.sessionSpotEndpoint=tcp://$($sessionSpot.Host):$($sessionSpot.Port) -Dzlink.samples.bingo.sessionRouterEndpoint=tcp://$($sessionRouter.Host):$($sessionRouter.Port) -Dzlink.samples.bingo.playSpotEndpoint=tcp://$($playSpot.Host):$($playSpot.Port) -Dzlink.samples.bingo.playSpotRouterEndpoint=tcp://$($playRouter.Host):$($playRouter.Port) -Dzlink.samples.bingo.sessionRouteEndpoint=tcp://$($sessionRoute.Host):$($sessionRoute.Port) -Dzlink.samples.bingo.playRouteEndpoint=tcp://$($playRoute.Host):$($playRoute.Port) -Dzlink.samples.bingo.streamEndpoint=tcp://$($stream.Host):$($stream.Port)"

    Push-Location "../../.."
    try {
        & ./gradlew --no-daemon :zlink-framework-core:jar :zlink-framework-spring-boot-starter:jar :zlink-framework-codec-protobuf:jar :zlink-stream-connector:jar --quiet
        if ($LASTEXITCODE -ne 0) { throw "Framework jar build failed" }
    } finally {
        Pop-Location
    }

    Invoke-Gradle @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", "classes", "--quiet")

    Start-GradleRole -Arguments @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", ":Server:Registry:run", "--quiet") -LogName "registry.log"
    Wait-Port $registryPub.Host $registryPub.Port
    Wait-Port $registryRouter.Host $registryRouter.Port

    Start-GradleRole -Arguments @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", ":Server:Session:run", "--quiet") -LogName "session.log"
    Wait-Port $sessionRoute.Host $sessionRoute.Port
    Wait-Port $sessionSpot.Host $sessionSpot.Port
    Wait-Port $sessionRouter.Host $sessionRouter.Port
    Wait-Port $stream.Host $stream.Port

    Start-GradleRole -Arguments @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", ":Server:Api:run", "--quiet") -LogName "api.log"
    Wait-Port $apiChannel.Host $apiChannel.Port

    Start-GradleRole -Arguments @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", ":Server:Play:run", "--quiet") -LogName "play.log"
    Wait-Port $playChannel.Host $playChannel.Port
    Wait-Port $playRoute.Host $playRoute.Port
    Wait-Port $playRouter.Host $playRouter.Port
    Wait-Port $playSpot.Host $playSpot.Port

    Invoke-Gradle @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", ":Probe:run", "--quiet")
    Invoke-Gradle @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", ":Client:run", "--quiet")
    $Status = 0
} finally {
    Cleanup $Status
    $env:JAVA_TOOL_OPTIONS = $oldJavaToolOptions
}
