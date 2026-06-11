Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$runDir = Join-Path ([System.IO.Path]::GetTempPath()) ("zlink-tictactoe-ts-" + [System.Guid]::NewGuid().ToString("N"))
$logDir = Join-Path $runDir "logs"
New-Item -ItemType Directory -Path $logDir | Out-Null
$processes = New-Object System.Collections.Generic.List[System.Diagnostics.Process]

function Get-FreePorts([int]$Count) {
    $listeners = New-Object System.Collections.Generic.List[System.Net.Sockets.TcpListener]
    $ports = New-Object System.Collections.Generic.List[int]
    try {
        for ($i = 0; $i -lt $Count; $i++) {
            $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Parse("127.0.0.1"), 0)
            $listener.Start()
            $listeners.Add($listener)
            $ports.Add($listener.LocalEndpoint.Port)
        }
        return $ports.ToArray()
    }
    finally {
        foreach ($listener in $listeners) {
            $listener.Stop()
        }
    }
}

function Get-EndpointParts([string]$Endpoint) {
    $value = $Endpoint -replace '^tcp://', '' -replace '^http://', ''
    $index = $value.LastIndexOf(':')
    return @{ Host = $value.Substring(0, $index); Port = [int]$value.Substring($index + 1) }
}

function Wait-Port([string]$Name, [string]$Endpoint) {
    $parts = Get-EndpointParts $Endpoint
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while ([DateTime]::UtcNow -lt $deadline) {
        $client = [System.Net.Sockets.TcpClient]::new()
        try {
            $task = $client.ConnectAsync($parts.Host, $parts.Port)
            if ($task.Wait(100)) {
                return
            }
        }
        catch {
        }
        finally {
            $client.Dispose()
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for $Name at $Endpoint"
}

function Start-Server([string]$Name, [string]$Entry) {
    $process = Start-Process -FilePath "node" `
        -ArgumentList @((Join-Path $scriptDir $Entry)) `
        -WorkingDirectory $scriptDir `
        -RedirectStandardOutput (Join-Path $logDir "$Name.log") `
        -RedirectStandardError (Join-Path $logDir "$Name.err.log") `
        -PassThru
    $processes.Add($process)
}

try {
    $ports = Get-FreePorts 4
    $env:TICTACTOE_API_ENDPOINT = if ($env:TICTACTOE_API_ENDPOINT) { $env:TICTACTOE_API_ENDPOINT } else { "tcp://127.0.0.1:$($ports[0])" }
    $env:TICTACTOE_PLAY_ENDPOINT = if ($env:TICTACTOE_PLAY_ENDPOINT) { $env:TICTACTOE_PLAY_ENDPOINT } else { "tcp://127.0.0.1:$($ports[1])" }
    $env:TICTACTOE_PLAY_STREAM_ENDPOINT = if ($env:TICTACTOE_PLAY_STREAM_ENDPOINT) { $env:TICTACTOE_PLAY_STREAM_ENDPOINT } else { "tcp://127.0.0.1:$($ports[2])" }
    $env:TICTACTOE_API_HTTP_ENDPOINT = if ($env:TICTACTOE_API_HTTP_ENDPOINT) { $env:TICTACTOE_API_HTTP_ENDPOINT } else { "http://127.0.0.1:$($ports[3])" }
    $env:ZLINK_SAMPLE_CONFIG = Join-Path $runDir "sample.config.json"

    @{
        sample = @{
            apiEndpoint = $env:TICTACTOE_API_ENDPOINT
            apiHttpEndpoint = $env:TICTACTOE_API_HTTP_ENDPOINT
            playEndpoint = $env:TICTACTOE_PLAY_ENDPOINT
            playStreamEndpoint = $env:TICTACTOE_PLAY_STREAM_ENDPOINT
        }
    } | ConvertTo-Json -Depth 8 | Set-Content -Path $env:ZLINK_SAMPLE_CONFIG -Encoding UTF8

    Push-Location $scriptDir
    try {
        npm run build | Out-Host
    }
    finally {
        Pop-Location
    }

    Start-Server "play" "dist/Server/Play/main.js"
    Wait-Port "play-channel" $env:TICTACTOE_PLAY_ENDPOINT
    Wait-Port "play-stream" $env:TICTACTOE_PLAY_STREAM_ENDPOINT

    Start-Server "api" "dist/Server/Api/main.js"
    Wait-Port "api-channel" $env:TICTACTOE_API_ENDPOINT
    Wait-Port "api-http" $env:TICTACTOE_API_HTTP_ENDPOINT

    node (Join-Path $scriptDir "dist/Client/main.js")
}
finally {
    for ($i = $processes.Count - 1; $i -ge 0; $i--) {
        $process = $processes[$i]
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
    }
    if ($env:TICTACTOE_TS_KEEP_RUN_DIR -ne "1") {
        Remove-Item -Recurse -Force $runDir -ErrorAction SilentlyContinue
    }
    else {
        Write-Host "runDir=$runDir"
    }
}
