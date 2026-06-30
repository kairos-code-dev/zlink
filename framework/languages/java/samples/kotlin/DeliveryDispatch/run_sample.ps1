Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$SampleDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $SampleDir

$LogDir = Join-Path $SampleDir "build/sample-logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $LogDir "*.log")

$Gradle = if ($IsWindows) { Join-Path $SampleDir "../../gradlew.bat" } else { Join-Path $SampleDir "../../gradlew" }
$RolePattern = "systems\.zlink\.samples\.kotlin\.deliverydispatch\.(server\.(registry|dispatchapi|dispatchcenter|courier|tracking|session)\.Program|client\.Program|probe\.Program)"
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

function Wait-Http {
    param([string]$Url, [int]$TimeoutSeconds = 60)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            Invoke-WebRequest -Uri "$Url/health" -UseBasicParsing -TimeoutSec 2 | Out-Null
            return
        } catch {
            Start-Sleep -Milliseconds 100
        }
    }
    throw "Timed out waiting for $Url/health"
}

function Split-Endpoint {
    param([string]$Endpoint)
    $parts = $Endpoint.Split(":")
    return @{ Host = $parts[0]; Port = [int]$parts[1] }
}

function Invoke-Gradle {
    param([string[]]$Arguments)
    & $Gradle @("--max-workers", "1") @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Gradle failed: $($Arguments -join ' ')"
    }
}

function Start-GradleRole {
    param([string[]]$Arguments, [string]$LogName)
    $logPath = Join-Path $LogDir $LogName
    $errorLogPath = Join-Path $LogDir ($LogName + ".err.log")
    $process = Start-Process -FilePath $Gradle -ArgumentList (@("--max-workers", "1") + $Arguments) -WorkingDirectory $SampleDir -NoNewWindow -RedirectStandardOutput $logPath -RedirectStandardError $errorLogPath -PassThru
    $Processes.Add($process)
}

$Status = 1
$oldJavaToolOptions = $env:JAVA_TOOL_OPTIONS
try {
    $endpoints = Reserve-Endpoints 12
    $registryPub = Split-Endpoint $endpoints[0]
    $registryRouter = Split-Endpoint $endpoints[1]
    $apiHttp = Split-Endpoint $endpoints[2]
    $dispatchChannel = Split-Endpoint $endpoints[3]
    $courierA = Split-Endpoint $endpoints[4]
    $courierB = Split-Endpoint $endpoints[5]
    $trackingChannel = Split-Endpoint $endpoints[6]
    $statusFanout = Split-Endpoint $endpoints[7]
    $trackingSpotRouter = Split-Endpoint $endpoints[8]
    $sessionStream = Split-Endpoint $endpoints[9]
    $sessionSpotRouter = Split-Endpoint $endpoints[10]
    $spotRoute = Split-Endpoint $endpoints[11]

    $prefix = "zlink.samples.deliverydispatch"
    $env:JAVA_TOOL_OPTIONS = "$oldJavaToolOptions -D$prefix.registryPubEndpoint=tcp://$($registryPub.Host):$($registryPub.Port) -D$prefix.registryRouterEndpoint=tcp://$($registryRouter.Host):$($registryRouter.Port) -D$prefix.apiHttpUrl=http://$($apiHttp.Host):$($apiHttp.Port) -D$prefix.dispatchChannelEndpoint=tcp://$($dispatchChannel.Host):$($dispatchChannel.Port) -D$prefix.courierAEndpoint=tcp://$($courierA.Host):$($courierA.Port) -D$prefix.courierBEndpoint=tcp://$($courierB.Host):$($courierB.Port) -D$prefix.trackingChannelEndpoint=tcp://$($trackingChannel.Host):$($trackingChannel.Port) -D$prefix.statusFanoutEndpoint=tcp://$($statusFanout.Host):$($statusFanout.Port) -D$prefix.trackingSpotRouterEndpoint=tcp://$($trackingSpotRouter.Host):$($trackingSpotRouter.Port) -D$prefix.sessionStreamEndpoint=tcp://$($sessionStream.Host):$($sessionStream.Port) -D$prefix.sessionSpotRouterEndpoint=tcp://$($sessionSpotRouter.Host):$($sessionSpotRouter.Port) -D$prefix.trackingSpotRouteEndpoint=tcp://$($spotRoute.Host):$($spotRoute.Port)"

    Invoke-Gradle @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", "classes", "--quiet")

    Start-GradleRole -Arguments @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", ":Server:Registry:run", "--quiet") -LogName "registry.log"
    Wait-Port $registryPub.Host $registryPub.Port
    Wait-Port $registryRouter.Host $registryRouter.Port

    Start-GradleRole -Arguments @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", ":Server:Tracking:run", "--quiet") -LogName "tracking.log"
    Wait-Port $trackingChannel.Host $trackingChannel.Port
    Wait-Port $statusFanout.Host $statusFanout.Port
    Wait-Port $trackingSpotRouter.Host $trackingSpotRouter.Port

    Start-GradleRole -Arguments @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", ":Server:Session:run", "--quiet") -LogName "session.log"
    Wait-Port $sessionStream.Host $sessionStream.Port

    Start-GradleRole -Arguments @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", ":Server:Courier:run", "--args=--courier courier-a --mode timeout-reassign", "--quiet") -LogName "courier-a.log"
    Wait-Port $courierA.Host $courierA.Port

    Start-GradleRole -Arguments @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", ":Server:Courier:run", "--args=--courier courier-b --mode accept", "--quiet") -LogName "courier-b.log"
    Wait-Port $courierB.Host $courierB.Port

    Start-GradleRole -Arguments @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", ":Server:DispatchCenter:run", "--quiet") -LogName "dispatch-center.log"
    Wait-Port $dispatchChannel.Host $dispatchChannel.Port

    Start-GradleRole -Arguments @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", ":Server:DispatchApi:run", "--quiet") -LogName "dispatch-api.log"
    Wait-Http "http://$($apiHttp.Host):$($apiHttp.Port)"

    Invoke-Gradle @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", ":Probe:run", "--quiet")
    Invoke-Gradle @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", ":Client:run", "--quiet")
    $Status = 0
} finally {
    Cleanup $Status
    $env:JAVA_TOOL_OPTIONS = $oldJavaToolOptions
}
