$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "../sample_runner.ps1")

$RunDir = New-SampleRunDirectory "shoppingmall-dotnet"
$RunId = "$PID-$([Guid]::NewGuid().ToString('N'))"
$RedisContainer = $null
$RunSucceeded = $false
$LogDir = Join-Path $RunDir "logs"
$SampleLogDir = Join-Path $RunDir "sample-logs"
New-Item -ItemType Directory -Force -Path $LogDir, $SampleLogDir | Out-Null

try {
    $ports = New-SamplePorts -Count 12 -BasePort 0

    $env:SHOPPINGMALL_LOG_DIR = $SampleLogDir
    $env:SHOPPINGMALL_REDIS_KEY_PREFIX = "shoppingmall:dotnet:${RunId}:"
    $env:SHOPPINGMALL_API_A_HTTP_URL = "http://127.0.0.1:$($ports[0])"
    $env:SHOPPINGMALL_API_B_HTTP_URL = "http://127.0.0.1:$($ports[1])"
    $env:SHOPPINGMALL_WORKFLOW_A_HTTP_URL = "http://127.0.0.1:$($ports[4])"
    $env:SHOPPINGMALL_WORKFLOW_B_HTTP_URL = "http://127.0.0.1:$($ports[5])"
    $env:SHOPPINGMALL_WORKFLOW_A_CHANNEL_ENDPOINT = "tcp://127.0.0.1:$($ports[6])"
    $env:SHOPPINGMALL_WORKFLOW_B_CHANNEL_ENDPOINT = "tcp://127.0.0.1:$($ports[7])"
    $env:SHOPPINGMALL_WORKFLOW_A_SPOT_ENDPOINT = "tcp://127.0.0.1:$($ports[8])"
    $env:SHOPPINGMALL_WORKFLOW_A_SPOT_ROUTER_ENDPOINT = "tcp://127.0.0.1:$($ports[9])"
    $env:SHOPPINGMALL_WORKFLOW_B_SPOT_ENDPOINT = "tcp://127.0.0.1:$($ports[10])"
    $env:SHOPPINGMALL_WORKFLOW_B_SPOT_ROUTER_ENDPOINT = "tcp://127.0.0.1:$($ports[11])"

    Invoke-SampleDotnetBuild (Join-Path $ScriptDir "ShoppingMall.csproj")

    $redis = Start-SampleRedisContainer "zlink-shoppingmall-dotnet-redis"
    $RedisContainer = $redis.ContainerId
    $env:SHOPPINGMALL_REDIS_ENDPOINT = $redis.Endpoint
    Wait-SampleTcpEndpoint "redis" "tcp://$env:SHOPPINGMALL_REDIS_ENDPOINT"
    $baseSettings = [ordered]@{
        LogDirectory = $SampleLogDir
        RedisEndpoint = $env:SHOPPINGMALL_REDIS_ENDPOINT
        RedisKeyPrefix = $env:SHOPPINGMALL_REDIS_KEY_PREFIX
        ApiAHttpUrl = $env:SHOPPINGMALL_API_A_HTTP_URL
        ApiBHttpUrl = $env:SHOPPINGMALL_API_B_HTTP_URL
        WorkflowAHttpUrl = $env:SHOPPINGMALL_WORKFLOW_A_HTTP_URL
        WorkflowBHttpUrl = $env:SHOPPINGMALL_WORKFLOW_B_HTTP_URL
        WorkflowAChannelEndpoint = $env:SHOPPINGMALL_WORKFLOW_A_CHANNEL_ENDPOINT
        WorkflowBChannelEndpoint = $env:SHOPPINGMALL_WORKFLOW_B_CHANNEL_ENDPOINT
        WorkflowASpotEndpoint = $env:SHOPPINGMALL_WORKFLOW_A_SPOT_ENDPOINT
        WorkflowASpotRouterEndpoint = $env:SHOPPINGMALL_WORKFLOW_A_SPOT_ROUTER_ENDPOINT
        WorkflowBSpotEndpoint = $env:SHOPPINGMALL_WORKFLOW_B_SPOT_ENDPOINT
        WorkflowBSpotRouterEndpoint = $env:SHOPPINGMALL_WORKFLOW_B_SPOT_ROUTER_ENDPOINT
    }
    $configFiles = @{}
    foreach ($instance in @("workflow-a", "workflow-b", "api-a", "api-b", "client")) {
        $sample = [ordered]@{}
        foreach ($key in $baseSettings.Keys) { $sample[$key] = $baseSettings[$key] }
        $sample.InstanceId = $instance
        $path = Join-Path $RunDir "appsettings.$instance.json"
        @{ Sample = $sample } | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 -Path $path
        $configFiles[$instance] = $path
    }

    Start-SampleDotnetAssembly -Name "workflow-a" -Project (Join-Path $ScriptDir "Server/OrderWorkflow/ShoppingMall.OrderWorkflow.csproj") -LogDirectory $LogDir -Arguments @("--config", $configFiles["workflow-a"]) | Out-Null
    Wait-SampleTcpEndpoint "workflow-a-channel" $env:SHOPPINGMALL_WORKFLOW_A_CHANNEL_ENDPOINT
    Wait-SampleTcpEndpoint "workflow-a-spot-router" $env:SHOPPINGMALL_WORKFLOW_A_SPOT_ROUTER_ENDPOINT
    Wait-SampleTcpEndpoint "workflow-a-spot-pub" $env:SHOPPINGMALL_WORKFLOW_A_SPOT_ENDPOINT
    Wait-SampleHttpHealth "workflow-a" $env:SHOPPINGMALL_WORKFLOW_A_HTTP_URL

    Start-SampleDotnetAssembly -Name "workflow-b" -Project (Join-Path $ScriptDir "Server/OrderWorkflow/ShoppingMall.OrderWorkflow.csproj") -LogDirectory $LogDir -Arguments @("--config", $configFiles["workflow-b"]) | Out-Null
    Wait-SampleTcpEndpoint "workflow-b-channel" $env:SHOPPINGMALL_WORKFLOW_B_CHANNEL_ENDPOINT
    Wait-SampleTcpEndpoint "workflow-b-spot-router" $env:SHOPPINGMALL_WORKFLOW_B_SPOT_ROUTER_ENDPOINT
    Wait-SampleTcpEndpoint "workflow-b-spot-pub" $env:SHOPPINGMALL_WORKFLOW_B_SPOT_ENDPOINT
    Wait-SampleHttpHealth "workflow-b" $env:SHOPPINGMALL_WORKFLOW_B_HTTP_URL

    Start-SampleDotnetAssembly -Name "api-a" -Project (Join-Path $ScriptDir "Server/CommerceApi/ShoppingMall.CommerceApi.csproj") -LogDirectory $LogDir -Arguments @("--config", $configFiles["api-a"]) | Out-Null
    Wait-SampleHttpHealth "api-a" $env:SHOPPINGMALL_API_A_HTTP_URL

    Start-SampleDotnetAssembly -Name "api-b" -Project (Join-Path $ScriptDir "Server/CommerceApi/ShoppingMall.CommerceApi.csproj") -LogDirectory $LogDir -Arguments @("--config", $configFiles["api-b"]) | Out-Null
    Wait-SampleHttpHealth "api-b" $env:SHOPPINGMALL_API_B_HTTP_URL

    Invoke-SampleDotnetRun -Project (Join-Path $ScriptDir "Client/ShoppingMall.Client.csproj") -Arguments @("--config", $configFiles["client"])

    Assert-SampleLogContains -LogDirectory $SampleLogDir -Pattern "shoppingmall=completed"
    if (-not (Select-String -Path (Join-Path $LogDir "workflow-a.out.log") -SimpleMatch "shoppingmall order: started" -Quiet)) {
        throw "workflow-a did not record a shoppingmall order start."
    }
    if (-not (Select-String -Path (Join-Path $LogDir "workflow-b.out.log") -SimpleMatch "shoppingmall order: started" -Quiet)) {
        throw "workflow-b did not record a shoppingmall order start."
    }
    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "shoppingmall evidence:"
    Assert-SampleLogContains -LogDirectory $SampleLogDir -Pattern "message flow"
    Write-Host "shoppingmall-server-evidence=completed"
    $RunSucceeded = $true
}
finally {
    Stop-SampleProcesses
    if ($RedisContainer) {
        Remove-SampleRedisContainer $RedisContainer
    }
    if (-not $RunSucceeded -or $env:SHOPPINGMALL_KEEP_RUN_DIR -eq "1") {
        Write-Host "runDir=$RunDir"
    }
    else {
        Remove-Item -Recurse -Force $RunDir -ErrorAction SilentlyContinue
    }
}
