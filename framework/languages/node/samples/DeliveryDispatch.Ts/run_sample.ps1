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

function New-FreePort {
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Parse("127.0.0.1"), 0)
    $listener.Start()
    $port = $listener.LocalEndpoint.Port
    $listener.Stop()
    return $port
}

function Get-EndpointHost {
    param([string]$Endpoint)
    $value = $Endpoint -replace '^tcp://', '' -replace '^http://', ''
    return ($value -split ':')[0]
}

function Get-EndpointPort {
    param([string]$Endpoint)
    $value = $Endpoint -replace '^tcp://', '' -replace '^http://', ''
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
    $name = "deliverydispatch-node-redis-$([System.Guid]::NewGuid().ToString("N"))"
    $containerId = (& docker run -d --rm --tmpfs /data --name $name -p "127.0.0.1::6379" redis:7.2-alpine).Trim()
    if (-not $containerId) {
        throw "Failed to start Redis container"
    }
    $portLine = (& docker port $containerId "6379/tcp").Trim()
    $port = ($portLine -split ':')[-1]
    $env:DELIVERYDISPATCH_REDIS_ENDPOINT = "127.0.0.1:$port"
    Wait-Port "redis" "tcp://$env:DELIVERYDISPATCH_REDIS_ENDPOINT"
    return $containerId
}

Push-Location $scriptDir
try {
    npm run build | Out-Null

    $runDir = Join-Path ([System.IO.Path]::GetTempPath()) ([System.Guid]::NewGuid().ToString("N"))
    $script:logDir = Join-Path $runDir "logs"
    $workDir = Join-Path $runDir "work"
    if (-not $env:DELIVERYDISPATCH_LOG_DIR) {
        $env:DELIVERYDISPATCH_LOG_DIR = Join-Path $scriptDir "logs"
    }
    $env:DELIVERYDISPATCH_WORK_DIR = $workDir
    New-Item -ItemType Directory -Force -Path $script:logDir, $workDir, $env:DELIVERYDISPATCH_LOG_DIR | Out-Null
    Remove-Item (Join-Path $env:DELIVERYDISPATCH_LOG_DIR "*.log") -Force -ErrorAction SilentlyContinue

    $ports = 1..15 | ForEach-Object { New-FreePort }
    $env:DELIVERYDISPATCH_API_HTTP = "http://127.0.0.1:$($ports[0])"
    $env:DELIVERYDISPATCH_CENTER_ROUTE = "tcp://127.0.0.1:$($ports[1])"
    $env:DELIVERYDISPATCH_COURIER_ROUTE = "tcp://127.0.0.1:$($ports[2])"
    $env:DELIVERYDISPATCH_COURIER_STREAM = "tcp://127.0.0.1:$($ports[3])"
    $env:DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTE = "tcp://127.0.0.1:$($ports[4])"
    $env:DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTE = "tcp://127.0.0.1:$($ports[5])"
    $env:DELIVERYDISPATCH_COURIER_ACTOR_NODE1_SPOT = "tcp://127.0.0.1:$($ports[6])"
    $env:DELIVERYDISPATCH_COURIER_ACTOR_NODE2_SPOT = "tcp://127.0.0.1:$($ports[7])"
    $env:DELIVERYDISPATCH_TRACKING_ROUTE = "tcp://127.0.0.1:$($ports[8])"
    $env:DELIVERYDISPATCH_STATUS_FANOUT = "tcp://127.0.0.1:$($ports[9])"
    $env:DELIVERYDISPATCH_TRACKING_SPOT_ROUTER = "tcp://127.0.0.1:$($ports[10])"
    $env:DELIVERYDISPATCH_TRACKING_SPOT = "tcp://127.0.0.1:$($ports[11])"
	    $env:DELIVERYDISPATCH_SESSION_STREAM = "tcp://127.0.0.1:$($ports[12])"
	    $env:DELIVERYDISPATCH_SESSION_SPOT_ROUTER = "tcp://127.0.0.1:$($ports[13])"
	    $env:DELIVERYDISPATCH_SESSION_SPOT = "tcp://127.0.0.1:$($ports[14])"
	    if (-not $env:DELIVERYDISPATCH_REDIS_KEY_PREFIX) {
	        $env:DELIVERYDISPATCH_REDIS_KEY_PREFIX = "deliverydispatch:node:$([System.Guid]::NewGuid().ToString("N")):"
	    }
	    $redisContainerId = Start-Redis

	    Start-Role "tracking"
    Wait-Port "tracking-route" $env:DELIVERYDISPATCH_TRACKING_ROUTE

    Start-Role "session"
    Wait-Port "session-stream" $env:DELIVERYDISPATCH_SESSION_STREAM
    Start-Sleep -Seconds 1

    Start-Role "courier-session"
    Wait-Port "courier-session-stream" $env:DELIVERYDISPATCH_COURIER_STREAM

    Start-Role "courier-actor-node1" @("--mode", "timeout-reassign")
    Wait-Port "courier-actor-node1" $env:DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTE
    Wait-Port "courier-actor-node1-spot" $env:DELIVERYDISPATCH_COURIER_ACTOR_NODE1_SPOT

    Start-Role "courier-actor-node2" @("--mode", "accept")
    Wait-Port "courier-actor-node2" $env:DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTE
    Wait-Port "courier-actor-node2-spot" $env:DELIVERYDISPATCH_COURIER_ACTOR_NODE2_SPOT

    Start-Role "courier-gateway"
    Wait-Port "courier-gateway" $env:DELIVERYDISPATCH_COURIER_ROUTE

    Start-Role "dispatch-center"
    Wait-Port "dispatch-center" $env:DELIVERYDISPATCH_CENTER_ROUTE

    Start-Role "dispatch-api"
    Wait-Http "dispatch-api" $env:DELIVERYDISPATCH_API_HTTP

    node (Join-Path $scriptDir "dist/Server/main.js") --role probe --timeout-ms 10000
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    node (Join-Path $scriptDir "dist/Client/main.js")
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    if (-not (Select-String -Path (Join-Path $script:logDir "tracking.out.log") -Pattern "deliverydispatch tracking: status" -Quiet)) {
        throw "Missing tracking evidence"
    }
    if (-not (Select-String -Path (Join-Path $script:logDir "session.out.log") -Pattern "deliverydispatch session: bound customer" -Quiet)) {
        throw "Missing session evidence"
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
	        & docker rm -f $redisContainerId | Out-Null
	    }
    if ($env:DELIVERYDISPATCH_KEEP_RUN_DIR -eq "1") {
        Write-Output "runDir=$runDir"
    } elseif ($runDir -and (Test-Path $runDir)) {
        Remove-Item $runDir -Recurse -Force -ErrorAction SilentlyContinue
    }
    Pop-Location
}
