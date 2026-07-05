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
$RolePattern = "systems\.zlink\.samples\.kotlin\.shoppingmall\.(server\.(registry|commerceapi|orderworkflow)\.ProgramKt|client\.ProgramKt)"
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
    $endpoints = Reserve-Endpoints 6
    $registryPub = Split-Endpoint $endpoints[0]
    $registryRouter = Split-Endpoint $endpoints[1]
    $commerceA = Split-Endpoint $endpoints[2]
    $commerceB = Split-Endpoint $endpoints[3]
    $workflowA = Split-Endpoint $endpoints[4]
    $workflowB = Split-Endpoint $endpoints[5]

    $prefix = "zlink.samples.shoppingmall"
    $env:JAVA_TOOL_OPTIONS = "$oldJavaToolOptions -D$prefix.registryPubEndpoint=tcp://$($registryPub.Host):$($registryPub.Port) -D$prefix.registryRouterEndpoint=tcp://$($registryRouter.Host):$($registryRouter.Port) -D$prefix.commerceApiAEndpoint=tcp://$($commerceA.Host):$($commerceA.Port) -D$prefix.commerceApiBEndpoint=tcp://$($commerceB.Host):$($commerceB.Port) -D$prefix.workflowAEndpoint=tcp://$($workflowA.Host):$($workflowA.Port) -D$prefix.workflowBEndpoint=tcp://$($workflowB.Host):$($workflowB.Port) -D$prefix.storeDir=$StoreDir"

    Push-Location "../../.."
    try {
        & ./gradlew --no-daemon :zlink-framework-core:jar :zlink-framework-spring-boot-starter:jar :zlink-framework-kotlin:jar --quiet
        if ($LASTEXITCODE -ne 0) { throw "Framework jar build failed" }
    } finally {
        Pop-Location
    }

    Invoke-Gradle @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", "classes", "--quiet")

    Start-GradleRole -Arguments @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", ":Server:Registry:run", "--quiet") -LogName "registry.log"
    Wait-Port $registryPub.Host $registryPub.Port
    Wait-Port $registryRouter.Host $registryRouter.Port

    Start-GradleRole -Arguments @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", ":Server:OrderWorkflow:run", "--args=--instance workflow-a", "--quiet") -LogName "workflow-a.log"
    Wait-Port $workflowA.Host $workflowA.Port

    Start-GradleRole -Arguments @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", ":Server:OrderWorkflow:run", "--args=--instance workflow-b", "--quiet") -LogName "workflow-b.log"
    Wait-Port $workflowB.Host $workflowB.Port

    Start-GradleRole -Arguments @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", ":Server:CommerceApi:run", "--args=--instance api-a", "--quiet") -LogName "api-a.log"
    Wait-Port $commerceA.Host $commerceA.Port

    Start-GradleRole -Arguments @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", ":Server:CommerceApi:run", "--args=--instance api-b", "--quiet") -LogName "api-b.log"
    Wait-Port $commerceB.Host $commerceB.Port

    Invoke-Gradle @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", ":Client:run", "--quiet")
    $Status = 0
} finally {
    Cleanup $Status
    $env:JAVA_TOOL_OPTIONS = $oldJavaToolOptions
}
