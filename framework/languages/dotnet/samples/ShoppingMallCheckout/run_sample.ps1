$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "../sample_runner.ps1")

$RunDir = New-SampleRunDirectory "shoppingmall-dotnet"
$LogDir = Join-Path $RunDir "logs"
$StoreDir = Join-Path $RunDir "store"
New-Item -ItemType Directory -Force -Path $LogDir, $StoreDir | Out-Null

function Set-DefaultEnv {
    param([string]$Name, [string]$Value)
    if (-not [Environment]::GetEnvironmentVariable($Name, "Process")) {
        [Environment]::SetEnvironmentVariable($Name, $Value, "Process")
    }
}

try {
    $basePort = if ($env:SHOPPINGMALL_BASE_PORT) { [int]$env:SHOPPINGMALL_BASE_PORT } else { 0 }
    $ports = New-SamplePorts -Count 14 -BasePort $basePort

    Set-DefaultEnv "SHOPPINGMALL_REGISTRY_PUB_ENDPOINT" "tcp://127.0.0.1:$($ports[0])"
    Set-DefaultEnv "SHOPPINGMALL_REGISTRY_ROUTER_ENDPOINT" "tcp://127.0.0.1:$($ports[1])"
    Set-DefaultEnv "SHOPPINGMALL_API_A_HTTP_URL" "http://127.0.0.1:$($ports[2])"
    Set-DefaultEnv "SHOPPINGMALL_API_B_HTTP_URL" "http://127.0.0.1:$($ports[3])"
    Set-DefaultEnv "SHOPPINGMALL_API_A_ROUTE_ENDPOINT" "tcp://127.0.0.1:$($ports[4])"
    Set-DefaultEnv "SHOPPINGMALL_API_B_ROUTE_ENDPOINT" "tcp://127.0.0.1:$($ports[5])"
    Set-DefaultEnv "SHOPPINGMALL_WORKFLOW_A_HTTP_URL" "http://127.0.0.1:$($ports[6])"
    Set-DefaultEnv "SHOPPINGMALL_WORKFLOW_B_HTTP_URL" "http://127.0.0.1:$($ports[7])"
    Set-DefaultEnv "SHOPPINGMALL_WORKFLOW_A_ROUTE_ENDPOINT" "tcp://127.0.0.1:$($ports[8])"
    Set-DefaultEnv "SHOPPINGMALL_WORKFLOW_B_ROUTE_ENDPOINT" "tcp://127.0.0.1:$($ports[9])"
    Set-DefaultEnv "SHOPPINGMALL_WORKFLOW_A_SPOT_ENDPOINT" "tcp://127.0.0.1:$($ports[10])"
    Set-DefaultEnv "SHOPPINGMALL_WORKFLOW_A_SPOT_ROUTER_ENDPOINT" "tcp://127.0.0.1:$($ports[11])"
    Set-DefaultEnv "SHOPPINGMALL_WORKFLOW_B_SPOT_ENDPOINT" "tcp://127.0.0.1:$($ports[12])"
    Set-DefaultEnv "SHOPPINGMALL_WORKFLOW_B_SPOT_ROUTER_ENDPOINT" "tcp://127.0.0.1:$($ports[13])"
    Set-DefaultEnv "SHOPPINGMALL_STORE_DIR" $StoreDir

    Invoke-SampleDotnetBuild (Join-Path $ScriptDir "ShoppingMallCheckout.csproj")

    Start-SampleDotnetAssembly -Name "registry" -Project (Join-Path $ScriptDir "Server/Registry/ShoppingMall.Registry.csproj") -LogDirectory $LogDir | Out-Null
    Wait-SampleTcpEndpoint "registry-router" $env:SHOPPINGMALL_REGISTRY_ROUTER_ENDPOINT

    Start-SampleDotnetAssembly -Name "workflow-a" -Project (Join-Path $ScriptDir "Server/OrderWorkflow/ShoppingMall.OrderWorkflow.csproj") -LogDirectory $LogDir -Arguments @("--instance", "workflow-a") | Out-Null
    Wait-SampleTcpEndpoint "workflow-a-route" $env:SHOPPINGMALL_WORKFLOW_A_ROUTE_ENDPOINT
    Wait-SampleTcpEndpoint "workflow-a-spot-router" $env:SHOPPINGMALL_WORKFLOW_A_SPOT_ROUTER_ENDPOINT
    Wait-SampleTcpEndpoint "workflow-a-spot-pub" $env:SHOPPINGMALL_WORKFLOW_A_SPOT_ENDPOINT
    Wait-SampleHttpHealth "workflow-a" $env:SHOPPINGMALL_WORKFLOW_A_HTTP_URL

    Start-SampleDotnetAssembly -Name "workflow-b" -Project (Join-Path $ScriptDir "Server/OrderWorkflow/ShoppingMall.OrderWorkflow.csproj") -LogDirectory $LogDir -Arguments @("--instance", "workflow-b") | Out-Null
    Wait-SampleTcpEndpoint "workflow-b-route" $env:SHOPPINGMALL_WORKFLOW_B_ROUTE_ENDPOINT
    Wait-SampleTcpEndpoint "workflow-b-spot-router" $env:SHOPPINGMALL_WORKFLOW_B_SPOT_ROUTER_ENDPOINT
    Wait-SampleTcpEndpoint "workflow-b-spot-pub" $env:SHOPPINGMALL_WORKFLOW_B_SPOT_ENDPOINT
    Wait-SampleHttpHealth "workflow-b" $env:SHOPPINGMALL_WORKFLOW_B_HTTP_URL

    Start-SampleDotnetAssembly -Name "api-a" -Project (Join-Path $ScriptDir "Server/CommerceApi/ShoppingMall.CommerceApi.csproj") -LogDirectory $LogDir -Arguments @("--instance", "api-a") | Out-Null
    Wait-SampleTcpEndpoint "api-a-route" $env:SHOPPINGMALL_API_A_ROUTE_ENDPOINT
    Wait-SampleHttpHealth "api-a" $env:SHOPPINGMALL_API_A_HTTP_URL

    Start-SampleDotnetAssembly -Name "api-b" -Project (Join-Path $ScriptDir "Server/CommerceApi/ShoppingMall.CommerceApi.csproj") -LogDirectory $LogDir -Arguments @("--instance", "api-b") | Out-Null
    Wait-SampleTcpEndpoint "api-b-route" $env:SHOPPINGMALL_API_B_ROUTE_ENDPOINT
    Wait-SampleHttpHealth "api-b" $env:SHOPPINGMALL_API_B_HTTP_URL

    Invoke-SampleDotnetRun -Project (Join-Path $ScriptDir "Client/ShoppingMallCheckout.Client.csproj")

    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "shoppingmall order: started"
    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "shoppingmall evidence:"
    Write-Host "shoppingmall-server-evidence=completed"
}
finally {
    Stop-SampleProcesses
    if ($env:SHOPPINGMALL_KEEP_RUN_DIR -eq "1") {
        Write-Host "runDir=$RunDir"
    }
    else {
        Remove-Item -Recurse -Force $RunDir -ErrorAction SilentlyContinue
    }
}
