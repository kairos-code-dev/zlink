$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "../sample_runner.ps1")

$RunDir = New-SampleRunDirectory "deliverydispatch-dotnet"
$LogDir = Join-Path $RunDir "logs"
$WorkDir = Join-Path $RunDir "work"
New-Item -ItemType Directory -Force -Path $LogDir, $WorkDir | Out-Null

function Set-DefaultEnv {
    param([string]$Name, [string]$Value)
    if (-not [Environment]::GetEnvironmentVariable($Name, "Process")) {
        [Environment]::SetEnvironmentVariable($Name, $Value, "Process")
    }
}

try {
    $basePort = if ($env:DELIVERYDISPATCH_BASE_PORT) { [int]$env:DELIVERYDISPATCH_BASE_PORT } else { 0 }
    $ports = New-SamplePorts -Count 14 -BasePort $basePort

    Set-DefaultEnv "DELIVERYDISPATCH_REGISTRY_PUB" "tcp://127.0.0.1:$($ports[0])"
    Set-DefaultEnv "DELIVERYDISPATCH_REGISTRY" "tcp://127.0.0.1:$($ports[1])"
    Set-DefaultEnv "DELIVERYDISPATCH_API_HTTP" "http://127.0.0.1:$($ports[2])"
    Set-DefaultEnv "DELIVERYDISPATCH_CENTER_ROUTE" "tcp://127.0.0.1:$($ports[3])"
    Set-DefaultEnv "DELIVERYDISPATCH_COURIER_A_ROUTE" "tcp://127.0.0.1:$($ports[4])"
    Set-DefaultEnv "DELIVERYDISPATCH_COURIER_B_ROUTE" "tcp://127.0.0.1:$($ports[5])"
    Set-DefaultEnv "DELIVERYDISPATCH_TRACKING_ROUTE" "tcp://127.0.0.1:$($ports[6])"
    Set-DefaultEnv "DELIVERYDISPATCH_STATUS_FANOUT" "tcp://127.0.0.1:$($ports[7])"
    Set-DefaultEnv "DELIVERYDISPATCH_TRACKING_SPOT_ROUTER" "tcp://127.0.0.1:$($ports[8])"
    Set-DefaultEnv "DELIVERYDISPATCH_TRACKING_SPOT" "tcp://127.0.0.1:$($ports[9])"
    Set-DefaultEnv "DELIVERYDISPATCH_SESSION_STREAM" "tcp://127.0.0.1:$($ports[10])"
    Set-DefaultEnv "DELIVERYDISPATCH_SESSION_SPOT_ROUTER" "tcp://127.0.0.1:$($ports[11])"
    Set-DefaultEnv "DELIVERYDISPATCH_SESSION_SPOT" "tcp://127.0.0.1:$($ports[12])"
    Set-DefaultEnv "DELIVERYDISPATCH_WORK_DIR" $WorkDir

    Invoke-SampleDotnetBuild (Join-Path $ScriptDir "DeliveryDispatch.csproj")

    Start-SampleDotnetAssembly -Name "registry" -Project (Join-Path $ScriptDir "Server/Registry/DeliveryDispatch.Server.Registry.csproj") -LogDirectory $LogDir | Out-Null
    Wait-SampleTcpEndpoint "registry-router" $env:DELIVERYDISPATCH_REGISTRY

    Start-SampleDotnetAssembly -Name "tracking" -Project (Join-Path $ScriptDir "Server/Tracking/DeliveryDispatch.Server.Tracking.csproj") -LogDirectory $LogDir | Out-Null
    Wait-SampleTcpEndpoint "tracking-route" $env:DELIVERYDISPATCH_TRACKING_ROUTE
    Wait-SampleTcpEndpoint "status-fanout" $env:DELIVERYDISPATCH_STATUS_FANOUT

    Start-SampleDotnetAssembly -Name "session" -Project (Join-Path $ScriptDir "Server/Session/DeliveryDispatch.Server.Session.csproj") -LogDirectory $LogDir | Out-Null
    Wait-SampleTcpEndpoint "session-stream" $env:DELIVERYDISPATCH_SESSION_STREAM

    Start-SampleDotnetAssembly -Name "courier-a" -Project (Join-Path $ScriptDir "Server/Courier/DeliveryDispatch.Server.Courier.csproj") -LogDirectory $LogDir -Arguments @("--courier", "courier-a", "--mode", "timeout-reassign") | Out-Null
    Wait-SampleTcpEndpoint "courier-a" $env:DELIVERYDISPATCH_COURIER_A_ROUTE

    Start-SampleDotnetAssembly -Name "courier-b" -Project (Join-Path $ScriptDir "Server/Courier/DeliveryDispatch.Server.Courier.csproj") -LogDirectory $LogDir -Arguments @("--courier", "courier-b", "--mode", "accept") | Out-Null
    Wait-SampleTcpEndpoint "courier-b" $env:DELIVERYDISPATCH_COURIER_B_ROUTE

    Start-SampleDotnetAssembly -Name "dispatch-center" -Project (Join-Path $ScriptDir "Server/DispatchCenter/DeliveryDispatch.Server.DispatchCenter.csproj") -LogDirectory $LogDir | Out-Null
    Wait-SampleTcpEndpoint "dispatch-center" $env:DELIVERYDISPATCH_CENTER_ROUTE

    Start-SampleDotnetAssembly -Name "dispatch-api" -Project (Join-Path $ScriptDir "Server/DispatchApi/DeliveryDispatch.Server.DispatchApi.csproj") -LogDirectory $LogDir | Out-Null
    Wait-SampleHttpHealth "dispatch-api" $env:DELIVERYDISPATCH_API_HTTP

    Invoke-SampleDotnetRun -Project (Join-Path $ScriptDir "Probe/DeliveryDispatch.Probe.csproj") -Arguments @("--registry-endpoint", $env:DELIVERYDISPATCH_REGISTRY, "--timeout-seconds", "10")

    Start-Sleep -Seconds 3

    Invoke-SampleDotnetRun -Project (Join-Path $ScriptDir "Client/DeliveryDispatch.Client.csproj") -Arguments @("--api-url", $env:DELIVERYDISPATCH_API_HTTP, "--stream-endpoint", $env:DELIVERYDISPATCH_SESSION_STREAM)

    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "deliverydispatch tracking: status"
    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "deliverydispatch session: bound customer"
}
finally {
    Stop-SampleProcesses
    if ($env:DELIVERYDISPATCH_KEEP_RUN_DIR -eq "1") {
        Write-Host "runDir=$RunDir"
    }
    else {
        Remove-Item -Recurse -Force $RunDir -ErrorAction SilentlyContinue
    }
}
