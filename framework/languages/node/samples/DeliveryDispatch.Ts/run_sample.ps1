Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $IsWindows) {
    & bash (Join-Path $scriptDir "run_sample.sh")
    exit $LASTEXITCODE
}

$processes = New-Object System.Collections.Generic.List[System.Diagnostics.Process]
$runDir = $null
$redisContainerId = $null

function Invoke-Docker {
    param([string[]] $Arguments, [int] $TimeoutSeconds = 10)
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = "docker"
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $startInfo.ArgumentList.Add($argument) }
    $process = [System.Diagnostics.Process]::Start($startInfo)
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill($true)
        throw "docker $($Arguments -join ' ') timed out after ${TimeoutSeconds}s"
    }
    $output = $process.StandardOutput.ReadToEnd().Trim()
    $errorOutput = $process.StandardError.ReadToEnd().Trim()
    if ($process.ExitCode -ne 0) {
        throw "docker $($Arguments -join ' ') failed ($($process.ExitCode)): $errorOutput"
    }
    return $output
}

function Get-FreePorts([int] $Count) {
    $listeners = [System.Collections.Generic.List[System.Net.Sockets.TcpListener]]::new()
    $ports = [System.Collections.Generic.List[int]]::new()
    try {
        for ($i = 0; $i -lt $Count; $i++) {
            $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
            $listener.Start()
            $listeners.Add($listener)
            $ports.Add(([System.Net.IPEndPoint]$listener.LocalEndpoint).Port)
        }
        return $ports.ToArray()
    } finally {
        foreach ($listener in $listeners) { $listener.Stop() }
    }
}

function Get-EndpointHost {
    param([string]$Endpoint)
    $value = $Endpoint -replace '^tcp://', '' -replace '^ws://', '' -replace '^http://', ''
    return ($value -split ':')[0]
}

function Get-EndpointPort {
    param([string]$Endpoint)
    $value = $Endpoint -replace '^tcp://', '' -replace '^ws://', '' -replace '^http://', ''
    return [int](($value -split ':')[-1])
}

function Wait-Port {
    param([string]$Name, [string]$Endpoint)
    $hostName = Get-EndpointHost $Endpoint
    $port = Get-EndpointPort $Endpoint
    $deadline = (Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $deadline) {
        try {
            $client = [System.Net.Sockets.TcpClient]::new()
            $client.Connect($hostName, $port)
            $client.Close()
            return
        } catch {
            Start-Sleep -Milliseconds 100
        }
    }
    throw "Timed out waiting for $Name at $Endpoint"
}

function Wait-Http {
    param([string]$Name, [string]$Url)
    $deadline = (Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $deadline) {
        try {
            Invoke-WebRequest -Uri "$Url/health" -UseBasicParsing -TimeoutSec 2 | Out-Null
            return
        } catch {
            Start-Sleep -Milliseconds 100
        }
    }
    throw "Timed out waiting for $Name at $Url"
}

function Start-Role {
    param([string]$Name, [string[]]$Arguments = @())
    $outLog = Join-Path $script:logDir "$Name.out.log"
    $errLog = Join-Path $script:logDir "$Name.err.log"
    $server = Join-Path $script:scriptDir "dist/Server/main.js"
    $process = Start-Process -FilePath "node" `
        -ArgumentList (@($server, "--role", $Name) + $Arguments) `
        -RedirectStandardOutput $outLog `
        -RedirectStandardError $errLog `
        -PassThru
    $script:processes.Add($process)
}

function Start-Redis {
    $name = "zlink-redis-node-deliverydispatch-ps1-$([System.Guid]::NewGuid().ToString("N"))"
    $image = if ($env:ZLINK_REDIS_IMAGE) { $env:ZLINK_REDIS_IMAGE } else { "redis:7.2-alpine" }
    $containerId = Invoke-Docker -Arguments @(
        "create", "--name", $name, "--tmpfs", "/data",
        "--label", "systems.zlink.sample=deliverydispatch-ts", "-p", "127.0.0.1::6379", $image
    )
    if (-not $containerId) {
        throw "Failed to create Redis container"
    }
    $script:redisContainerId = $containerId
    Invoke-Docker -Arguments @("start", $containerId) | Out-Null
    if ((Invoke-Docker -Arguments @("inspect", "-f", "{{.State.Running}}", $containerId)) -ne "true") {
        throw "Redis container is not running"
    }
    $ready = $false
    for ($i = 0; $i -lt 300; $i++) {
        try {
            if ((Invoke-Docker -Arguments @("exec", $containerId, "redis-cli", "PING") -TimeoutSeconds 5) -eq "PONG") {
                $ready = $true
                break
            }
        } catch {}
        Start-Sleep -Milliseconds 100
    }
    if (-not $ready) { throw "Redis container did not return exact PONG before timeout" }
    $port = Invoke-Docker -Arguments @("inspect", "-f", '{{(index (index .NetworkSettings.Ports "6379/tcp") 0).HostPort}}', $containerId)
    $env:DELIVERYDISPATCH_REDIS_ENDPOINT = "127.0.0.1:$port"
    return $containerId
}

Push-Location $scriptDir
try {
    npm run build | Out-Null

    $runDir = Join-Path ([System.IO.Path]::GetTempPath()) ([System.Guid]::NewGuid().ToString("N"))
    $script:logDir = Join-Path $runDir "logs"
    $workDir = Join-Path $runDir "work"
    if (-not $env:DELIVERYDISPATCH_LOG_DIR) {
        $env:DELIVERYDISPATCH_LOG_DIR = Join-Path $script:logDir "flow"
    }
    $env:DELIVERYDISPATCH_WORK_DIR = $workDir
    New-Item -ItemType Directory -Force -Path $script:logDir, $workDir, $env:DELIVERYDISPATCH_LOG_DIR | Out-Null
    Remove-Item (Join-Path $env:DELIVERYDISPATCH_LOG_DIR "*.log") -Force -ErrorAction SilentlyContinue

    $ports = Get-FreePorts 14
    $env:DELIVERYDISPATCH_API_HTTP = "http://127.0.0.1:$($ports[0])"
    $env:DELIVERYDISPATCH_CENTER_ROUTE = "tcp://127.0.0.1:$($ports[1])"
    $env:DELIVERYDISPATCH_COURIER_STREAM = "ws://127.0.0.1:$($ports[2])"
    $env:DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTE = "tcp://127.0.0.1:$($ports[3])"
    $env:DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTE = "tcp://127.0.0.1:$($ports[4])"
    $env:DELIVERYDISPATCH_COURIER_ACTOR_NODE1_SPOT = "tcp://127.0.0.1:$($ports[5])"
    $env:DELIVERYDISPATCH_COURIER_ACTOR_NODE2_SPOT = "tcp://127.0.0.1:$($ports[6])"
    $env:DELIVERYDISPATCH_TRACKING_ROUTE = "tcp://127.0.0.1:$($ports[7])"
    $env:DELIVERYDISPATCH_TRACKING_SPOT = "tcp://127.0.0.1:$($ports[8])"
    $env:DELIVERYDISPATCH_SESSION_STREAM = "ws://127.0.0.1:$($ports[9])"
    $env:DELIVERYDISPATCH_SESSION_SPOT_ROUTER = "tcp://127.0.0.1:$($ports[10])"
    $env:DELIVERYDISPATCH_COURIER_SESSION_SPOT = "tcp://127.0.0.1:$($ports[11])"
	    if (-not $env:DELIVERYDISPATCH_REDIS_KEY_PREFIX) {
	        $env:DELIVERYDISPATCH_REDIS_KEY_PREFIX = "deliverydispatch:node:$([System.Guid]::NewGuid().ToString("N")):"
	    }
	    $redisContainerId = Start-Redis

	    Start-Role "tracking"
    Wait-Port "tracking-route" $env:DELIVERYDISPATCH_TRACKING_ROUTE
    Wait-Port "tracking-customer-spot" $env:DELIVERYDISPATCH_TRACKING_SPOT

    Start-Role "customer-gateway"
    Wait-Port "session-stream" $env:DELIVERYDISPATCH_SESSION_STREAM
    Start-Sleep -Seconds 1

    Start-Role "courier-session"
    Wait-Port "courier-session-stream" $env:DELIVERYDISPATCH_COURIER_STREAM
    Wait-Port "courier-session-spot" $env:DELIVERYDISPATCH_COURIER_SESSION_SPOT

    Start-Role "courier-spot-node1"
    Wait-Port "courier-actor-node1" $env:DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTE
    Wait-Port "courier-actor-node1-spot" $env:DELIVERYDISPATCH_COURIER_ACTOR_NODE1_SPOT

    Start-Role "courier-spot-node2"
    Wait-Port "courier-actor-node2" $env:DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTE
    Wait-Port "courier-actor-node2-spot" $env:DELIVERYDISPATCH_COURIER_ACTOR_NODE2_SPOT

    Start-Role "dispatch"
    Wait-Port "dispatch-center" $env:DELIVERYDISPATCH_CENTER_ROUTE
    Wait-Http "dispatch-api" $env:DELIVERYDISPATCH_API_HTTP

    node (Join-Path $scriptDir "dist/Server/main.js") --role probe --timeout-ms 10000
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    node (Join-Path $scriptDir "../../scripts/browser-e2e/run-sample.mjs") "DeliveryDispatch.Ts"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    if (-not (Select-String -Path (Join-Path $script:logDir "tracking.out.log") -Pattern "deliverydispatch tracking: status" -Quiet)) {
        throw "Missing tracking evidence"
    }
    if (-not (Select-String -Path (Join-Path $script:logDir "customer-gateway.out.log") -Pattern "deliverydispatch session: bound customer" -Quiet)) {
        throw "Missing session evidence"
    }
    if (-not (Select-String -Path (Join-Path $script:logDir "customer-gateway.out.log") -Pattern "deliverydispatch session: found existing customer=customer-1" -Quiet)) {
        throw "Missing customer reconnect evidence"
    }
    if (-not (Select-String -Path (Join-Path $script:logDir "courier-session.out.log") -Pattern "deliverydispatch courier-session: found existing courier=courier-a" -Quiet)) {
        throw "Missing courier-a reconnect evidence"
    }
    if (-not (Select-String -Path (Join-Path $script:logDir "courier-session.out.log") -Pattern "deliverydispatch courier-session: found existing courier=courier-b" -Quiet)) {
        throw "Missing courier-b reconnect evidence"
    }
	    if (-not (Get-ChildItem $env:DELIVERYDISPATCH_LOG_DIR -Filter "*.log" | Select-String -Pattern "message flow" -Quiet)) {
	        throw "Missing message flow evidence"
	    }
	} finally {
	    for ($i = $processes.Count - 1; $i -ge 0; $i--) {
	        $process = $processes[$i]
	        if ($null -ne $process -and -not $process.HasExited) {
	            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
	        }
	    }
        if ($redisContainerId) {
            try { Invoke-Docker -Arguments @("rm", "-f", "-v", $redisContainerId) | Out-Null } catch {}
        }
    if ($env:DELIVERYDISPATCH_KEEP_RUN_DIR -eq "1") {
        Write-Output "runDir=$runDir"
    } elseif ($runDir -and (Test-Path $runDir)) {
        Remove-Item $runDir -Recurse -Force -ErrorAction SilentlyContinue
    }
    Pop-Location
}
