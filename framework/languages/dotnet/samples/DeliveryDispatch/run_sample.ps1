$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "../sample_runner.ps1")

$RunDir = New-SampleRunDirectory "deliverydispatch-dotnet"
$RedisContainer = $null
$LogDir = Join-Path $RunDir "logs"
$WorkDir = Join-Path $RunDir "work"
$SampleLogDir = if ($env:DELIVERYDISPATCH_LOG_DIR) { $env:DELIVERYDISPATCH_LOG_DIR } else { Join-Path $ScriptDir "logs" }
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
New-Item -ItemType Directory -Force -Path $SampleLogDir | Out-Null
Remove-Item -Path (Join-Path $SampleLogDir "*.log") -Force -ErrorAction SilentlyContinue
$env:DELIVERYDISPATCH_LOG_DIR = $SampleLogDir

function Set-DefaultEnv {
    param([string]$Name, [string]$Value)
    if (-not [Environment]::GetEnvironmentVariable($Name, "Process")) {
        [Environment]::SetEnvironmentVariable($Name, $Value, "Process")
    }
}

function Wait-SampleLogContains {
    param(
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Description,
        [int]$Attempts = 80
    )

    for ($i = 0; $i -lt $Attempts; $i++) {
        $match = Get-ChildItem -Path $SampleLogDir -Filter "*.log" |
            Select-String -Pattern $Pattern -List |
            Select-Object -First 1
        if ($null -ne $match) {
            return
        }
        Start-Sleep -Milliseconds 200
    }

    throw "$Description was not found."
}

try {
    $basePort = if ($env:DELIVERYDISPATCH_BASE_PORT) { [int]$env:DELIVERYDISPATCH_BASE_PORT } else { 0 }
    $ports = New-SamplePorts -Count 19 -BasePort $basePort

    Set-DefaultEnv "DELIVERYDISPATCH_REDIS_KEY_PREFIX" "deliverydispatch:dotnet:${PID}:$([Guid]::NewGuid().ToString('N')):"
    Set-DefaultEnv "DELIVERYDISPATCH_DISPATCH_HTTP" "http://127.0.0.1:$($ports[2])"
    Set-DefaultEnv "DELIVERYDISPATCH_DISPATCH_CHANNEL" "tcp://127.0.0.1:$($ports[3])"
    Set-DefaultEnv "DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTE" "tcp://127.0.0.1:$($ports[4])"
    Set-DefaultEnv "DELIVERYDISPATCH_TRACKING_ROUTE" "tcp://127.0.0.1:$($ports[5])"
    Set-DefaultEnv "DELIVERYDISPATCH_TRACKING_SPOT_ROUTER" "tcp://127.0.0.1:$($ports[6])"
    Set-DefaultEnv "DELIVERYDISPATCH_TRACKING_SPOT" "tcp://127.0.0.1:$($ports[7])"
    Set-DefaultEnv "DELIVERYDISPATCH_CUSTOMER_STREAM" "tcp://127.0.0.1:$($ports[8])"
    Set-DefaultEnv "DELIVERYDISPATCH_CUSTOMER_SPOT_ROUTER" "tcp://127.0.0.1:$($ports[9])"
    Set-DefaultEnv "DELIVERYDISPATCH_CUSTOMER_SPOT" "tcp://127.0.0.1:$($ports[10])"
    Set-DefaultEnv "DELIVERYDISPATCH_COURIER_STREAM" "tcp://127.0.0.1:$($ports[11])"
    Set-DefaultEnv "DELIVERYDISPATCH_COURIER_SESSION_SPOT_ROUTER" "tcp://127.0.0.1:$($ports[12])"
    Set-DefaultEnv "DELIVERYDISPATCH_COURIER_SESSION_SPOT" "tcp://127.0.0.1:$($ports[13])"
    Set-DefaultEnv "DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTER" "tcp://127.0.0.1:$($ports[14])"
    Set-DefaultEnv "DELIVERYDISPATCH_COURIER_ACTOR_NODE1" "tcp://127.0.0.1:$($ports[15])"
    Set-DefaultEnv "DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTE" "tcp://127.0.0.1:$($ports[16])"
    Set-DefaultEnv "DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTER" "tcp://127.0.0.1:$($ports[17])"
    Set-DefaultEnv "DELIVERYDISPATCH_COURIER_ACTOR_NODE2" "tcp://127.0.0.1:$($ports[18])"
    Set-DefaultEnv "DELIVERYDISPATCH_WORK_DIR" $WorkDir

    if (-not $env:DELIVERYDISPATCH_REDIS_ENDPOINT) {
        if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
            throw "Docker is required to run the DeliveryDispatch sample when DELIVERYDISPATCH_REDIS_ENDPOINT is not set."
        }

        $RedisContainer = "deliverydispatch-dotnet-redis-$PID-$([Guid]::NewGuid().ToString('N'))"
        & docker run -d --rm --name $RedisContainer -p "127.0.0.1::6379" redis:7.2-alpine | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to start Redis container."
        }
        $redisPort = (& docker port $RedisContainer "6379/tcp") -replace '^.*:', ''
        $env:DELIVERYDISPATCH_REDIS_ENDPOINT = "127.0.0.1:$redisPort"
    }
    Wait-SampleTcpEndpoint "redis" "tcp://$env:DELIVERYDISPATCH_REDIS_ENDPOINT"

    Invoke-SampleDotnetBuild (Join-Path $ScriptDir "DeliveryDispatch.sln")

    Start-SampleDotnetAssembly -Name "tracking" -Project (Join-Path $ScriptDir "Server/Tracking/DeliveryDispatch.Server.Tracking.csproj") -LogDirectory $LogDir | Out-Null
    Wait-SampleTcpEndpoint "tracking-route" $env:DELIVERYDISPATCH_TRACKING_ROUTE
    Wait-SampleTcpEndpoint "tracking-spot-router" $env:DELIVERYDISPATCH_TRACKING_SPOT_ROUTER
    Wait-SampleTcpEndpoint "tracking-spot" $env:DELIVERYDISPATCH_TRACKING_SPOT

    Start-SampleDotnetAssembly -Name "customer-gateway" -Project (Join-Path $ScriptDir "Server/CustomerGateway/DeliveryDispatch.Server.CustomerGateway.csproj") -LogDirectory $LogDir | Out-Null
    Wait-SampleTcpEndpoint "customer-stream" $env:DELIVERYDISPATCH_CUSTOMER_STREAM
    Wait-SampleTcpEndpoint "customer-spot-router" $env:DELIVERYDISPATCH_CUSTOMER_SPOT_ROUTER

    Start-SampleDotnetAssembly -Name "courier-session" -Project (Join-Path $ScriptDir "Server/CourierSession/DeliveryDispatch.Server.CourierSession.csproj") -LogDirectory $LogDir | Out-Null
    Wait-SampleTcpEndpoint "courier-session-stream" $env:DELIVERYDISPATCH_COURIER_STREAM

    Start-SampleDotnetAssembly -Name "courier-actor-node1" -Project (Join-Path $ScriptDir "Server/CourierActorNode/DeliveryDispatch.Server.CourierActorNode.csproj") -LogDirectory $LogDir -Arguments @("--node", "node1") | Out-Null
    Wait-SampleTcpEndpoint "courier-actor-node1-route" $env:DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTE
    Wait-SampleTcpEndpoint "courier-actor-node1-router" $env:DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTER

    Start-SampleDotnetAssembly -Name "courier-actor-node2" -Project (Join-Path $ScriptDir "Server/CourierActorNode/DeliveryDispatch.Server.CourierActorNode.csproj") -LogDirectory $LogDir -Arguments @("--node", "node2") | Out-Null
    Wait-SampleTcpEndpoint "courier-actor-node2-route" $env:DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTE
    Wait-SampleTcpEndpoint "courier-actor-node2-router" $env:DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTER

    Start-SampleDotnetAssembly -Name "dispatch" -Project (Join-Path $ScriptDir "Server/Dispatch/DeliveryDispatch.Server.Dispatch.csproj") -LogDirectory $LogDir | Out-Null
    Wait-SampleHttpHealth "dispatch" $env:DELIVERYDISPATCH_DISPATCH_HTTP

    $clientLog = Join-Path $LogDir "client.log"
    Invoke-SampleDotnetRun -Project (Join-Path $ScriptDir "Client/DeliveryDispatch.Client.csproj") -Arguments @("--api-url", $env:DELIVERYDISPATCH_DISPATCH_HTTP, "--stream-endpoint", $env:DELIVERYDISPATCH_CUSTOMER_STREAM, "--courier-stream-endpoint", $env:DELIVERYDISPATCH_COURIER_STREAM) *> $clientLog
    if (-not (Select-String -Path $clientLog -Pattern "deliverydispatch=completed" -Quiet)) {
        throw "DeliveryDispatch client did not complete."
    }
    if (-not (Select-String -Path $clientLog -Pattern "topology=ready" -Quiet)) {
        throw "DeliveryDispatch client did not print topology marker."
    }
    if (-not (Select-String -Path $clientLog -Pattern "deliverydispatch-reassignment=completed" -Quiet)) {
        throw "DeliveryDispatch client did not verify reassignment."
    }
    if (-not (Select-String -Path $clientLog -Pattern "deliverydispatch-server-evidence=completed" -Quiet)) {
        throw "DeliveryDispatch client did not verify server evidence."
    }

    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "deliverydispatch tracking: status"
    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "deliverydispatch customer-session: bound customer"
    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "deliverydispatch courier-session: bound courier=courier-a"
    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "deliverydispatch courier-session: bound courier=courier-b"
    Wait-SampleLogContains "message flow" "DeliveryDispatch message-flow evidence"
    Write-Host "deliverydispatch-runner-evidence=completed"
}
finally {
    Stop-SampleProcesses
    if ($RedisContainer) {
        & docker rm -f $RedisContainer | Out-Null
    }
    if ($env:DELIVERYDISPATCH_KEEP_RUN_DIR -eq "1") {
        Write-Host "runDir=$RunDir"
    }
    else {
        Remove-Item -Recurse -Force $RunDir -ErrorAction SilentlyContinue
    }
}
