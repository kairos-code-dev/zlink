$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "../sample_runner.ps1")

$RunDir = New-SampleRunDirectory "shoppingmall-dotnet"
$RedisContainer = $null
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

    Set-DefaultEnv "SHOPPINGMALL_REDIS_KEY_PREFIX" "shoppingmall:dotnet:${PID}:$([Guid]::NewGuid().ToString('N')):"
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

    Invoke-SampleDotnetBuild (Join-Path $ScriptDir "ShoppingMall.csproj")

    # The sample owns its Redis: a dedicated, throwaway container is the shared
    # location store every server registers into (no registry process exists).
    if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
        throw "Docker is required to run the ShoppingMall sample (it provisions a dedicated Redis container)."
    }
    $RedisContainer = "shoppingmall-dotnet-redis-$PID-$([Guid]::NewGuid().ToString('N'))"
    & docker run -d --rm --name $RedisContainer -p "127.0.0.1::6379" redis:7.2-alpine | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to start Redis container."
    }
    $redisPort = (& docker port $RedisContainer "6379/tcp") -replace '^.*:', ''
    $env:SHOPPINGMALL_REDIS_ENDPOINT = "127.0.0.1:$redisPort"
    Wait-SampleTcpEndpoint "redis" "tcp://$env:SHOPPINGMALL_REDIS_ENDPOINT"

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

    $startupDelaySeconds = if ($env:SHOPPINGMALL_STARTUP_DELAY_SECONDS) { [double]$env:SHOPPINGMALL_STARTUP_DELAY_SECONDS } else { 3.0 }
    Start-Sleep -Seconds $startupDelaySeconds
    Invoke-SampleDotnetRun -Project (Join-Path $ScriptDir "Client/ShoppingMall.Client.csproj")

    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "shoppingmall order: started"
    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "shoppingmall evidence:"
    Write-Host "shoppingmall-server-evidence=completed"
}
finally {
    Stop-SampleProcesses
    if ($RedisContainer) {
        & docker rm -f $RedisContainer | Out-Null
    }
    if ($env:SHOPPINGMALL_KEEP_RUN_DIR -eq "1") {
        Write-Host "runDir=$RunDir"
    }
    else {
        Remove-Item -Recurse -Force $RunDir -ErrorAction SilentlyContinue
    }
}
