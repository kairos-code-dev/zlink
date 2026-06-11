$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "../sample_runner.ps1")

$RunDir = New-SampleRunDirectory "supportchat-dotnet"
$LogDir = Join-Path $RunDir "logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

function Set-DefaultEnv {
    param([string]$Name, [string]$Value)
    if (-not [Environment]::GetEnvironmentVariable($Name, "Process")) {
        [Environment]::SetEnvironmentVariable($Name, $Value, "Process")
    }
}

try {
    $basePort = if ($env:SUPPORTCHAT_BASE_PORT) { [int]$env:SUPPORTCHAT_BASE_PORT } else { 0 }
    $ports = New-SamplePorts -Count 13 -BasePort $basePort

    Set-DefaultEnv "SUPPORTCHAT_REGISTRY_PUB_ENDPOINT" "tcp://127.0.0.1:$($ports[0])"
    Set-DefaultEnv "SUPPORTCHAT_REGISTRY_ROUTER_ENDPOINT" "tcp://127.0.0.1:$($ports[1])"
    Set-DefaultEnv "SUPPORTCHAT_API_CHANNEL_ENDPOINT" "tcp://127.0.0.1:$($ports[2])"
    Set-DefaultEnv "SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT" "tcp://127.0.0.1:$($ports[3])"
    Set-DefaultEnv "SUPPORTCHAT_SESSION_SPOT_ENDPOINT" "tcp://127.0.0.1:$($ports[4])"
    Set-DefaultEnv "SUPPORTCHAT_SESSION_ROUTER_ENDPOINT" "tcp://127.0.0.1:$($ports[5])"
    Set-DefaultEnv "SUPPORTCHAT_SUPPORT_ROUTER_ENDPOINT" "tcp://127.0.0.1:$($ports[6])"
    Set-DefaultEnv "SUPPORTCHAT_ENTRY_SPOT_ENDPOINT" "tcp://127.0.0.1:$($ports[7])"
    Set-DefaultEnv "SUPPORTCHAT_ENTRY_SPOT_ROUTER_ENDPOINT" "tcp://127.0.0.1:$($ports[8])"
    Set-DefaultEnv "SUPPORTCHAT_CONVERSATION_SPOT_ENDPOINT" "tcp://127.0.0.1:$($ports[9])"
    Set-DefaultEnv "SUPPORTCHAT_CONVERSATION_SPOT_ROUTER_ENDPOINT" "tcp://127.0.0.1:$($ports[10])"
    Set-DefaultEnv "SUPPORTCHAT_STREAM_ENDPOINT" "tcp://127.0.0.1:$($ports[11])"
    Set-DefaultEnv "SUPPORTCHAT_RECONNECT_STREAM_ENDPOINT" "tcp://127.0.0.1:$($ports[12])"

    Invoke-SampleDotnetBuild (Join-Path $ScriptDir "SupportChat.csproj")

    Start-SampleDotnetAssembly -Name "registry" -Project (Join-Path $ScriptDir "Server/Registry/SupportChat.Server.Registry.csproj") -LogDirectory $LogDir | Out-Null
    Wait-SampleTcpEndpoint "registry-router" $env:SUPPORTCHAT_REGISTRY_ROUTER_ENDPOINT

    Start-SampleDotnetAssembly -Name "support" -Project (Join-Path $ScriptDir "Server/Support/SupportChat.Server.Support.csproj") -LogDirectory $LogDir | Out-Null
    Wait-SampleTcpEndpoint "support-channel" $env:SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT
    Wait-SampleTcpEndpoint "support-spot-router" $env:SUPPORTCHAT_ENTRY_SPOT_ROUTER_ENDPOINT
    Wait-SampleTcpEndpoint "support-spot-pub" $env:SUPPORTCHAT_ENTRY_SPOT_ENDPOINT
    Wait-SampleTcpEndpoint "support-conversation-router" $env:SUPPORTCHAT_CONVERSATION_SPOT_ROUTER_ENDPOINT
    Wait-SampleTcpEndpoint "support-conversation-pub" $env:SUPPORTCHAT_CONVERSATION_SPOT_ENDPOINT

    Start-SampleDotnetAssembly -Name "api" -Project (Join-Path $ScriptDir "Server/Api/SupportChat.Server.Api.csproj") -LogDirectory $LogDir | Out-Null
    Wait-SampleTcpEndpoint "api" $env:SUPPORTCHAT_API_CHANNEL_ENDPOINT

    Start-SampleDotnetAssembly -Name "session" -Project (Join-Path $ScriptDir "Server/Session/SupportChat.Server.Session.csproj") -LogDirectory $LogDir | Out-Null
    Wait-SampleTcpEndpoint "session-route" $env:SUPPORTCHAT_SESSION_SPOT_ENDPOINT
    Wait-SampleTcpEndpoint "session-router" $env:SUPPORTCHAT_SESSION_ROUTER_ENDPOINT
    Wait-SampleTcpEndpoint "session-stream" $env:SUPPORTCHAT_STREAM_ENDPOINT

    Invoke-SampleDotnetRun -Project (Join-Path $ScriptDir "Probe/SupportChat.Probe.csproj") -Arguments @("--registry-endpoint", $env:SUPPORTCHAT_REGISTRY_ROUTER_ENDPOINT, "--timeout-seconds", "10")
    Invoke-SampleDotnetRun -Project (Join-Path $ScriptDir "Client/SupportChat.Client.csproj") -Arguments @("--stream-endpoint", $env:SUPPORTCHAT_STREAM_ENDPOINT)

    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "support conversation: created"
    Assert-SampleLogContains -LogDirectory $LogDir -Pattern "support conversation: actor joined"
    Write-Host "supportchat-server-evidence=completed"
}
finally {
    Stop-SampleProcesses
    if ($env:SUPPORTCHAT_KEEP_RUN_DIR -eq "1") {
        Write-Host "runDir=$RunDir"
    }
    else {
        Remove-Item -Recurse -Force $RunDir -ErrorAction SilentlyContinue
    }
}
