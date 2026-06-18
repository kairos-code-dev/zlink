Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$SampleDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $SampleDir

$LogDir = Join-Path $SampleDir "build/sample-logs"
$StoreDir = Join-Path $SampleDir "build/sample-store"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
New-Item -ItemType Directory -Force -Path $StoreDir | Out-Null
Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $LogDir "*.log")
Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $StoreDir "*")

$Gradle = if ($IsWindows) { Join-Path $SampleDir "../../gradlew.bat" } else { Join-Path $SampleDir "../../gradlew" }
$RolePattern = "systems\.zlink\.samples\.kotlin\.gamequest\.(server\.(registry|gameapi|questmission)\.ProgramKt|client\.ProgramKt)"
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

function Split-Endpoint {
    param([string]$Endpoint)
    $parts = $Endpoint.Split(":")
    return @{ Host = $parts[0]; Port = [int]$parts[1] }
}

function Wait-Port {
    param([string]$Endpoint, [int]$TimeoutSeconds = 60)
    $parts = Split-Endpoint $Endpoint
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $client = [System.Net.Sockets.TcpClient]::new()
        try {
            $connect = $client.BeginConnect($parts.Host, $parts.Port, $null, $null)
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
    throw "Timed out waiting for $Endpoint"
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
    $endpoints = Reserve-Endpoints 14
    $prefix = "zlink.samples.gamequest"
    $env:JAVA_TOOL_OPTIONS = "$oldJavaToolOptions " +
        "-D${prefix}.registryPubEndpoint=tcp://$($endpoints[0]) " +
        "-D${prefix}.registryRouterEndpoint=tcp://$($endpoints[1]) " +
        "-D${prefix}.fanoutPublisherAEndpoint=tcp://$($endpoints[2]) " +
        "-D${prefix}.fanoutPublisherBEndpoint=tcp://$($endpoints[3]) " +
        "-D${prefix}.gameApiAActionEndpoint=tcp://$($endpoints[4]) " +
        "-D${prefix}.gameApiBActionEndpoint=tcp://$($endpoints[5]) " +
        "-D${prefix}.gameApiAStreamEndpoint=tcp://$($endpoints[6]) " +
        "-D${prefix}.gameApiBStreamEndpoint=tcp://$($endpoints[7]) " +
        "-D${prefix}.missionAActionEndpoint=tcp://$($endpoints[8]) " +
        "-D${prefix}.missionBActionEndpoint=tcp://$($endpoints[9]) " +
        "-D${prefix}.missionASpotEndpoint=tcp://$($endpoints[10]) " +
        "-D${prefix}.missionASpotRouterEndpoint=tcp://$($endpoints[11]) " +
        "-D${prefix}.missionBSpotEndpoint=tcp://$($endpoints[12]) " +
        "-D${prefix}.missionBSpotRouterEndpoint=tcp://$($endpoints[13]) " +
        "-D${prefix}.storeDirectory=$StoreDir"

    Push-Location "../../.."
    try {
        & ./gradlew --no-daemon :zlink-framework-core:jar :zlink-framework-spring-boot-starter:jar :zlink-framework-kotlin:jar --quiet
        if ($LASTEXITCODE -ne 0) { throw "Framework jar build failed" }
    } finally {
        Pop-Location
    }

    Invoke-Gradle @("--no-daemon", "--settings-file", "standalone.settings.gradle.kts", "classes", "--quiet")

    Start-GradleRole @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", ":Server:Registry:run", "--quiet") "registry.log"
    Wait-Port $endpoints[0]
    Wait-Port $endpoints[1]

    Start-GradleRole @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", ":Server:QuestMission:run", "--args=--mission mission-a", "--quiet") "mission-a.log"
    Wait-Port $endpoints[8]
    Wait-Port $endpoints[10]
    Wait-Port $endpoints[11]

    Start-GradleRole @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", ":Server:QuestMission:run", "--args=--mission mission-b", "--quiet") "mission-b.log"
    Wait-Port $endpoints[9]
    Wait-Port $endpoints[12]
    Wait-Port $endpoints[13]

    Start-GradleRole @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", ":Server:GameApi:run", "--args=--api api-a", "--quiet") "api-a.log"
    Wait-Port $endpoints[2]
    Wait-Port $endpoints[4]
    Wait-Port $endpoints[6]

    Start-GradleRole @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", ":Server:GameApi:run", "--args=--api api-b", "--quiet") "api-b.log"
    Wait-Port $endpoints[3]
    Wait-Port $endpoints[5]
    Wait-Port $endpoints[7]

    Invoke-Gradle @("--no-daemon", "--settings-file", "standalone.settings.gradle.kts", ":Client:run", "--quiet")
    Select-String -Path (Join-Path $LogDir "mission-a.log") -Pattern "gamequest mission processed" | Out-Null
    Select-String -Path (Join-Path $LogDir "mission-b.log") -Pattern "gamequest mission processed" | Out-Null
    Select-String -Path (Join-Path $LogDir "api-a.log") -Pattern "gamequest evidence: passed=true" | Out-Null
    Write-Output "gamequest-server-evidence=completed"
    $Status = 0
} finally {
    $env:JAVA_TOOL_OPTIONS = $oldJavaToolOptions
    Cleanup $Status
}
