$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir "../sample_runner.ps1")

$RunDir = New-SampleRunDirectory "tictactoe-dotnet"
$LogDir = Join-Path $RunDir "logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

try {
    $basePort = if ($env:TICTACTOE_BASE_PORT) { [int]$env:TICTACTOE_BASE_PORT } else { 0 }
    $ports = New-SamplePorts -Count 6 -BasePort $basePort

    $apiBindUrl = if ($env:TICTACTOE_API_BIND_URL) { $env:TICTACTOE_API_BIND_URL } else { "http://127.0.0.1:$($ports[0])" }
    $apiPublicUrl = if ($env:TICTACTOE_API_PUBLIC_URL) { $env:TICTACTOE_API_PUBLIC_URL } else { $apiBindUrl }
    $apiChannelEndpoint = if ($env:TICTACTOE_API_CHANNEL_ENDPOINT) { $env:TICTACTOE_API_CHANNEL_ENDPOINT } else { "tcp://127.0.0.1:$($ports[1])" }
    $playChannelEndpoint = if ($env:TICTACTOE_PLAY_CHANNEL_ENDPOINT) { $env:TICTACTOE_PLAY_CHANNEL_ENDPOINT } else { "tcp://127.0.0.1:$($ports[2])" }
    $playRouterEndpoint = if ($env:TICTACTOE_PLAY_ROUTER_ENDPOINT) { $env:TICTACTOE_PLAY_ROUTER_ENDPOINT } else { "tcp://127.0.0.1:$($ports[3])" }
    $playEndpoint = if ($env:TICTACTOE_PLAY_ENDPOINT) { $env:TICTACTOE_PLAY_ENDPOINT } else { "tcp://127.0.0.1:$($ports[4])" }
    $spotEndpoint = if ($env:TICTACTOE_SPOT_ENDPOINT) { $env:TICTACTOE_SPOT_ENDPOINT } else { "tcp://127.0.0.1:$($ports[5])" }
    $configFile = Join-Path $RunDir "appsettings.json"

    $settings = @{
        Sample = @{
            ApiBindUrl = $apiBindUrl
            ApiPublicUrl = $apiPublicUrl
            ApiChannelEndpoint = $apiChannelEndpoint
            PlayChannelEndpoint = $playChannelEndpoint
            PlayRouterEndpoint = $playRouterEndpoint
            PlayEndpoint = $playEndpoint
            SpotEndpoint = $spotEndpoint
            LogDirectory = $LogDir
        }
    }
    $settings | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 -Path $configFile

    Invoke-SampleDotnetBuild (Join-Path $ScriptDir "TicTacToe.sln")

    Start-SampleDotnetAssembly -Name "play" -Project (Join-Path $ScriptDir "Server/TicTacToe.Server.csproj") -LogDirectory $LogDir -Arguments @("play", "--config", $configFile) | Out-Null
    Wait-SampleTcpEndpoint "play-stream" $playEndpoint
    Wait-SampleTcpEndpoint "play-channel" $playChannelEndpoint
    Wait-SampleTcpEndpoint "play-router" $playRouterEndpoint

    Start-SampleDotnetAssembly -Name "api" -Project (Join-Path $ScriptDir "Server/TicTacToe.Server.csproj") -LogDirectory $LogDir -Arguments @("api", "--config", $configFile) | Out-Null
    Wait-SampleTcpEndpoint "api-http" $apiBindUrl
    Wait-SampleTcpEndpoint "api-channel" $apiChannelEndpoint

    Invoke-SampleDotnetRun -Project (Join-Path $ScriptDir "Client/TicTacToe.Client.csproj") -Arguments @("--api-url", $apiPublicUrl)
}
finally {
    Stop-SampleProcesses
    if ($env:TICTACTOE_KEEP_RUN_DIR -eq "1") {
        Write-Host "runDir=$RunDir"
    }
    else {
        Remove-Item -Recurse -Force $RunDir -ErrorAction SilentlyContinue
    }
}
