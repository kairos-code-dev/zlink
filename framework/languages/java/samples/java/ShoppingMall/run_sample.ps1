Set-StrictMode -Version Latest
. "$PSScriptRoot/../../redis-common.ps1"
$ErrorActionPreference = "Stop"

$SampleDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $SampleDir

$LogDir = Join-Path $SampleDir "build/sample-logs"
$FlowLogDir = if ($env:SHOPPINGMALL_LOG_DIR) { $env:SHOPPINGMALL_LOG_DIR } else { Join-Path $SampleDir "logs" }
New-Item -ItemType Directory -Force -Path $LogDir, $FlowLogDir | Out-Null
Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $LogDir "*.log")
Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $FlowLogDir "*.log")

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

function Get-ChildProcessIds {
    param([int]$ParentId)
    if ($IsWindows) {
        Get-CimInstance Win32_Process -Filter "ParentProcessId=$ParentId" | ForEach-Object {
            [int]$_.ProcessId
            Get-ChildProcessIds -ParentId ([int]$_.ProcessId)
        }
    } else {
        & pgrep -P $ParentId 2>$null | ForEach-Object {
            if ($_ -match '^\d+$') {
                [int]$_
                Get-ChildProcessIds -ParentId ([int]$_)
            }
        }
    }
}

function Stop-TrackedProcessTree {
    param([System.Diagnostics.Process]$Process)
    $children = @(Get-ChildProcessIds -ParentId $Process.Id)
    [array]::Reverse($children)
    foreach ($childId in $children) {
        Stop-Process -Id $childId -Force -ErrorAction SilentlyContinue
    }
    if (-not $Process.HasExited) {
        Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
    }
}

function Cleanup {
    param([int]$Status)
    Print-Logs $Status
    for ($i = $Processes.Count - 1; $i -ge 0; $i--) {
        Stop-TrackedProcessTree -Process $Processes[$i]
    }
    if ($RedisContainer) {
        Remove-ZlinkSampleRedis $RedisContainer
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

function Split-Endpoint {
    param([string]$Endpoint)
    $parts = $Endpoint.Split(":")
    return @{ Host = $parts[0]; Port = [int]$parts[1] }
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

function Invoke-Gradle {
    param([string[]]$Arguments)
    & $Gradle @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Gradle failed: $($Arguments -join ' ')"
    }
}

function Start-Role {
    param([string]$ScriptPath, [string]$LogName, [string]$JavaToolOptions)
    $logPath = Join-Path $LogDir $LogName
    $errorLogPath = Join-Path $LogDir ($LogName + ".err.log")
    $previous = $env:JAVA_TOOL_OPTIONS
    try {
        $env:JAVA_TOOL_OPTIONS = $JavaToolOptions
        $process = Start-Process -FilePath $ScriptPath -WorkingDirectory $SampleDir -NoNewWindow -RedirectStandardOutput $logPath -RedirectStandardError $errorLogPath -PassThru
        $Processes.Add($process)
    } finally {
        $env:JAVA_TOOL_OPTIONS = $previous
    }
}

function App-Bin {
    param([string]$Project, [string]$Script)
    $bin = Join-Path $SampleDir "$Project/build/install/$Script/bin"
    if ($IsWindows) {
        return Join-Path $bin "$Script.bat"
    }
    return Join-Path $bin $Script
}

$Status = 1
$oldJavaToolOptions = $env:JAVA_TOOL_OPTIONS
$oldLogDir = $env:SHOPPINGMALL_LOG_DIR
try {
    $endpoints = Reserve-Endpoints 8
    $apiAHttp = Split-Endpoint $endpoints[0]
    $apiBHttp = Split-Endpoint $endpoints[1]
    $workflowAChannel = Split-Endpoint $endpoints[2]
    $workflowBChannel = Split-Endpoint $endpoints[3]
    $workflowASpot = Split-Endpoint $endpoints[4]
    $workflowBSpot = Split-Endpoint $endpoints[5]
    $workflowARouter = Split-Endpoint $endpoints[6]
    $workflowBRouter = Split-Endpoint $endpoints[7]

    $redis = Start-ZlinkSampleRedis "zlink-redis-java-sample-shoppingmall"
    $RedisContainer = $redis.ContainerId
    $redisEndpoint = $redis.Endpoint
    $redis = Split-Endpoint $redisEndpoint
    Wait-Port $redis.Host $redis.Port

    $redisKeyPrefix = if ($env:SHOPPINGMALL_REDIS_KEY_PREFIX) { $env:SHOPPINGMALL_REDIS_KEY_PREFIX } else { "shoppingmall:java:${PID}:$([Guid]::NewGuid().ToString('N')):" }
    $env:SHOPPINGMALL_LOG_DIR = $FlowLogDir
    $commonJavaOptions = "$oldJavaToolOptions -Dzlink.samples.shoppingmall.apiAHttpUrl=http://$($apiAHttp.Host):$($apiAHttp.Port) -Dzlink.samples.shoppingmall.apiBHttpUrl=http://$($apiBHttp.Host):$($apiBHttp.Port) -Dzlink.samples.shoppingmall.workflowAChannelEndpoint=tcp://$($workflowAChannel.Host):$($workflowAChannel.Port) -Dzlink.samples.shoppingmall.workflowBChannelEndpoint=tcp://$($workflowBChannel.Host):$($workflowBChannel.Port) -Dzlink.samples.shoppingmall.workflowASpotEndpoint=tcp://$($workflowASpot.Host):$($workflowASpot.Port) -Dzlink.samples.shoppingmall.workflowBSpotEndpoint=tcp://$($workflowBSpot.Host):$($workflowBSpot.Port) -Dzlink.samples.shoppingmall.workflowASpotRouterEndpoint=tcp://$($workflowARouter.Host):$($workflowARouter.Port) -Dzlink.samples.shoppingmall.workflowBSpotRouterEndpoint=tcp://$($workflowBRouter.Host):$($workflowBRouter.Port) -Dzlink.samples.shoppingmall.redisEndpoint=$redisEndpoint -Dzlink.samples.shoppingmall.redisKeyPrefix=$redisKeyPrefix"

    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue `
        (Join-Path $SampleDir "Server/CommerceApi/build/install/CommerceApi"), `
        (Join-Path $SampleDir "Server/OrderWorkflow/build/install/OrderWorkflow"), `
        (Join-Path $SampleDir "Client/build/install/Client")

    Invoke-Gradle @("--settings-file", "standalone.settings.gradle.kts", "--no-daemon", ":Server:CommerceApi:installDist", ":Server:OrderWorkflow:installDist", ":Client:installDist", "--quiet")

    Start-Role -ScriptPath (App-Bin "Server/OrderWorkflow" "OrderWorkflow") -LogName "workflow-a.log" -JavaToolOptions "$commonJavaOptions -Dzlink.samples.shoppingmall.workflowName=workflow-a"
    Start-Role -ScriptPath (App-Bin "Server/OrderWorkflow" "OrderWorkflow") -LogName "workflow-b.log" -JavaToolOptions "$commonJavaOptions -Dzlink.samples.shoppingmall.workflowName=workflow-b"
    Wait-Port $workflowAChannel.Host $workflowAChannel.Port
    Wait-Port $workflowBChannel.Host $workflowBChannel.Port
    Wait-Port $workflowASpot.Host $workflowASpot.Port
    Wait-Port $workflowBSpot.Host $workflowBSpot.Port
    Wait-Port $workflowARouter.Host $workflowARouter.Port
    Wait-Port $workflowBRouter.Host $workflowBRouter.Port

    Start-Role -ScriptPath (App-Bin "Server/CommerceApi" "CommerceApi") -LogName "api-a.log" -JavaToolOptions "$commonJavaOptions -Dzlink.samples.shoppingmall.apiName=api-a"
    Start-Role -ScriptPath (App-Bin "Server/CommerceApi" "CommerceApi") -LogName "api-b.log" -JavaToolOptions "$commonJavaOptions -Dzlink.samples.shoppingmall.apiName=api-b"
    Wait-Port $apiAHttp.Host $apiAHttp.Port
    Wait-Port $apiBHttp.Host $apiBHttp.Port

    Write-Host "topology=ready"
    $clientLog = Join-Path $LogDir "client.log"
    $env:JAVA_TOOL_OPTIONS = $commonJavaOptions
    & (App-Bin "Client" "Client") *> $clientLog
    if ($LASTEXITCODE -ne 0) {
        throw "Client run failed."
    }
    Get-Content -Path $clientLog

    if (-not (Select-String -Path $clientLog -Pattern "shoppingmall-server-evidence=completed" -Quiet)) {
        throw "Server evidence marker was not found."
    }
    if (-not (Select-String -Path $clientLog -Pattern "shoppingmall=completed" -Quiet)) {
        throw "Client completion marker was not found."
    }
    if (-not (Select-String -Path (Join-Path $FlowLogDir "*.log") -Pattern "message flow" -Quiet)) {
        throw "Message flow evidence was not found."
    }
    Write-Host "shoppingmall full client/server self-check completed"
    $Status = 0
} finally {
    Cleanup $Status
    $env:JAVA_TOOL_OPTIONS = $oldJavaToolOptions
    $env:SHOPPINGMALL_LOG_DIR = $oldLogDir
}
