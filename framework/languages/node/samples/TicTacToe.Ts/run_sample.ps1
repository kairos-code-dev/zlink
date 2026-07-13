Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not $IsWindows) {
    & bash (Join-Path $PSScriptRoot "run_sample.sh")
    exit $LASTEXITCODE
}

$processes = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()
$redisContainer = $null
$runDir = $null

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
    }
    finally {
        foreach ($listener in $listeners) { $listener.Stop() }
    }
}

function Get-EndpointParts([string] $Endpoint) {
    $value = $Endpoint -replace '^tcp://', '' -replace '^ws://', '' -replace '^http://', '' -replace '^redis://', ''
    $index = $value.LastIndexOf(':')
    return @{ Host = $value.Substring(0, $index); Port = [int]$value.Substring($index + 1) }
}

function Wait-Port([string] $Name, [string] $Endpoint, [System.Diagnostics.Process] $Process = $null) {
    $parts = Get-EndpointParts $Endpoint
    $deadline = [DateTime]::UtcNow.AddSeconds(60)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($null -ne $Process -and $Process.HasExited) {
            throw "$Name process exited before accepting connections at $Endpoint"
        }
        $client = [System.Net.Sockets.TcpClient]::new()
        try {
            $task = $client.ConnectAsync($parts.Host, $parts.Port)
            if ($task.Wait(100)) { return }
        }
        catch {}
        finally { $client.Dispose() }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for $Name at $Endpoint"
}

function Wait-LogPattern {
    param(
        [string] $Name,
        [string[]] $Paths,
        [string] $Pattern,
        [System.Diagnostics.Process] $Process = $null
    )
    $deadline = [DateTime]::UtcNow.AddSeconds(60)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($null -ne $Process -and $Process.HasExited) {
            throw "$Name process exited before writing '$Pattern'"
        }
        foreach ($path in $Paths) {
            if ((Test-Path $path) -and (Select-String -Path $path -SimpleMatch $Pattern -Quiet)) { return }
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for $Name marker '$Pattern'"
}

function Start-Redis {
    $name = "zlink-redis-node-tictactoe-ps1-$([System.Guid]::NewGuid().ToString('N'))"
    $image = if ($env:ZLINK_REDIS_IMAGE) { $env:ZLINK_REDIS_IMAGE } else { "redis:7.2-alpine" }
    $id = Invoke-Docker -Arguments @(
        "create", "--name", $name, "--tmpfs", "/data",
        "--label", "systems.zlink.sample=tictactoe-ts", "-p", "127.0.0.1::6379", $image
    )
    if (-not $id) { throw "Failed to create the dedicated Redis container" }
    $script:redisContainer = $id
    Invoke-Docker -Arguments @("start", $id) | Out-Null
    if ((Invoke-Docker -Arguments @("inspect", "-f", "{{.State.Running}}", $id)) -ne "true") {
        throw "Redis container is not running"
    }
    $ready = $false
    for ($i = 0; $i -lt 300; $i++) {
        try {
            if ((Invoke-Docker -Arguments @("exec", $id, "redis-cli", "PING") -TimeoutSeconds 5) -eq "PONG") {
                $ready = $true
                break
            }
        }
        catch {}
        Start-Sleep -Milliseconds 100
    }
    if (-not $ready) { throw "Redis container did not return exact PONG before timeout" }
    $port = Invoke-Docker -Arguments @(
        "inspect", "-f", "{{(index (index .NetworkSettings.Ports `"6379/tcp`") 0).HostPort}}", $id
    )
    if (-not $port) { throw "Redis host port was not assigned" }
    return "127.0.0.1:$port"
}

function Write-SampleConfig {
    param(
        [string] $Path,
        [string] $InstanceName,
        [int] $ApiIndex,
        [int] $PlayIndex,
        [int] $PeerPlayIndex,
        [hashtable] $Topology
    )
    $sample = @{
        instanceName = $InstanceName
        apiIndex = $ApiIndex
        playIndex = $PlayIndex
        apiHttpEndpoint = $Topology.ApiHttpEndpoints[$ApiIndex]
        apiEndpoints = $Topology.ApiEndpoints
        apiHttpEndpoints = $Topology.ApiHttpEndpoints
        playEndpoint = $Topology.PlayChannelEndpoints[$PlayIndex]
        playChannelEndpoints = $Topology.PlayChannelEndpoints
        playEndpoints = $Topology.PlayStreamEndpoints
        playSpotEndpoint = $Topology.PlaySpotEndpoints[$PlayIndex]
        playSpotEndpoints = $Topology.PlaySpotEndpoints
        playSpotPubSubEndpoint = $Topology.PlayPubSubEndpoints[$PlayIndex]
        playSpotPubSubEndpoints = $Topology.PlayPubSubEndpoints
        playStreamEndpoint = $Topology.PlayStreamEndpoints[$PlayIndex]
        redisEndpoint = $Topology.RedisEndpoint
        redisKeyPrefix = $Topology.RedisKeyPrefix
        playSpotNodeRid = "play-node-$($PlayIndex + 1)"
        peerPlaySpotNodeRid = "play-node-$($PeerPlayIndex + 1)"
        peerPlaySpotEndpoint = $Topology.PlaySpotEndpoints[$PeerPlayIndex]
        peerPlaySpotPubEndpoint = $Topology.PlayPubSubEndpoints[$PeerPlayIndex]
    }
    $json = @{ sample = $sample } | ConvertTo-Json -Depth 8
    [System.IO.File]::WriteAllText($Path, $json, [System.Text.UTF8Encoding]::new($false))
}

function Start-Server([string] $Name, [string] $Entry, [string] $ConfigPath) {
    $previousConfig = $env:ZLINK_SAMPLE_CONFIG
    try {
        $env:ZLINK_SAMPLE_CONFIG = $ConfigPath
        $process = Start-Process -FilePath "node" `
            -ArgumentList @((Join-Path $PSScriptRoot $Entry)) `
            -WorkingDirectory $PSScriptRoot `
            -RedirectStandardOutput (Join-Path $script:logDir "$Name.log") `
            -RedirectStandardError (Join-Path $script:logDir "$Name.err.log") `
            -PassThru
        $script:processes.Add($process)
        return $process
    }
    finally {
        $env:ZLINK_SAMPLE_CONFIG = $previousConfig
    }
}

Push-Location $PSScriptRoot
try {
    npm run build | Out-Null
    $runDir = Join-Path ([System.IO.Path]::GetTempPath()) ("zlink-tictactoe-ts-" + [System.Guid]::NewGuid().ToString("N"))
    $script:logDir = Join-Path $runDir "logs"
    New-Item -ItemType Directory -Force -Path $script:logDir | Out-Null
    $env:TICTACTOE_LOG_DIR = Join-Path $runDir "flow-logs"
    New-Item -ItemType Directory -Force -Path $env:TICTACTOE_LOG_DIR | Out-Null

    $ports = Get-FreePorts 12
    $topology = @{
        ApiHttpEndpoints = @("http://127.0.0.1:$($ports[0])", "http://127.0.0.1:$($ports[1])")
        ApiEndpoints = @("tcp://127.0.0.1:$($ports[2])", "tcp://127.0.0.1:$($ports[3])")
        PlayChannelEndpoints = @("tcp://127.0.0.1:$($ports[4])", "tcp://127.0.0.1:$($ports[5])")
        PlayStreamEndpoints = @("ws://127.0.0.1:$($ports[6])", "ws://127.0.0.1:$($ports[7])")
        PlaySpotEndpoints = @("tcp://127.0.0.1:$($ports[8])", "tcp://127.0.0.1:$($ports[9])")
        PlayPubSubEndpoints = @("tcp://127.0.0.1:$($ports[10])", "tcp://127.0.0.1:$($ports[11])")
        RedisEndpoint = Start-Redis
        RedisKeyPrefix = "zlink:sample:tictactoe:ps1:$([System.Guid]::NewGuid().ToString('N')):"
    }
    $env:TICTACTOE_API_A_HTTP_ENDPOINT = $topology.ApiHttpEndpoints[0]
    Wait-Port -Name "redis" -Endpoint $topology.RedisEndpoint

    $apiAConfig = Join-Path $runDir "sample.api-a.json"
    $apiBConfig = Join-Path $runDir "sample.api-b.json"
    $playAConfig = Join-Path $runDir "sample.play-a.json"
    $playBConfig = Join-Path $runDir "sample.play-b.json"
    Write-SampleConfig $apiAConfig "api-a" 0 0 1 $topology
    Write-SampleConfig $apiBConfig "api-b" 1 0 1 $topology
    Write-SampleConfig $playAConfig "play-a" 0 0 1 $topology
    Write-SampleConfig $playBConfig "play-b" 0 1 0 $topology

    $playB = Start-Server "play-b" "dist/Server/Play/main.js" $playBConfig
    Wait-LogPattern "play-b" @((Join-Path $script:logDir "play-b.log")) $topology.PlaySpotEndpoints[1] $playB
    Wait-Port "play-b-channel" $topology.PlayChannelEndpoints[1] $playB
    Wait-Port "play-b-stream" $topology.PlayStreamEndpoints[1] $playB
    Wait-Port "play-b-pubsub" $topology.PlayPubSubEndpoints[1] $playB

    $playA = Start-Server "play-a" "dist/Server/Play/main.js" $playAConfig
    Wait-LogPattern "play-a" @((Join-Path $script:logDir "play-a.log")) $topology.PlaySpotEndpoints[0] $playA
    Wait-Port "play-a-channel" $topology.PlayChannelEndpoints[0] $playA
    Wait-Port "play-a-stream" $topology.PlayStreamEndpoints[0] $playA
    Wait-Port "play-a-pubsub" $topology.PlayPubSubEndpoints[0] $playA
    Wait-LogPattern "play-a peer" @((Join-Path $script:logDir "play-a.log")) '"event":"spotPeerReady"' $playA
    Wait-LogPattern "play-b peer" @((Join-Path $script:logDir "play-b.log")) '"event":"spotPeerReady"' $playB

    $apiA = Start-Server "api-a" "dist/Server/Api/main.js" $apiAConfig
    Wait-LogPattern "api-a" @((Join-Path $script:logDir "api-a.log")) $topology.ApiEndpoints[0] $apiA
    Wait-Port "api-a-channel" $topology.ApiEndpoints[0] $apiA
    Wait-Port "api-a-http" $topology.ApiHttpEndpoints[0] $apiA

    $apiB = Start-Server "api-b" "dist/Server/Api/main.js" $apiBConfig
    Wait-LogPattern "api-b" @((Join-Path $script:logDir "api-b.log")) $topology.ApiEndpoints[1] $apiB
    Wait-Port "api-b-channel" $topology.ApiEndpoints[1] $apiB
    Wait-Port "api-b-http" $topology.ApiHttpEndpoints[1] $apiB

    $clientLog = Join-Path $script:logDir "client.log"
    & node (Join-Path $PSScriptRoot "../../scripts/browser-e2e/run-sample.mjs") "TicTacToe.Ts" *> $clientLog
    if ($LASTEXITCODE -ne 0) { throw "TicTacToe client scenario failed" }
    foreach ($marker in @(
        "PASS TicTacToe.Ts",
        "stream-inbound sample=TicTacToe client=host",
        "stream-inbound sample=TicTacToe client=guest",
        "stream-inbound sample=TicTacToe client=observer",
        "observer-subscription=verified",
        "observer-win-milestone=verified"
    )) {
        if (-not (Select-String -Path $clientLog -SimpleMatch $marker -Quiet)) {
            throw "Missing client marker: $marker"
        }
    }
    if (-not (Select-String -Path $clientLog -Pattern "stream-inbound sample=TicTacToe .* seq=[0-9]" -Quiet)) {
        throw "TicTacToe client did not write a sequenced inbound response marker"
    }
    if (-not (Select-String -Path $clientLog -Pattern "stream-inbound sample=TicTacToe .* name=.*Notify" -Quiet)) {
        throw "TicTacToe client did not write an inbound push marker"
    }
    $playLogs = @((Join-Path $script:logDir "play-a.log"), (Join-Path $script:logDir "play-b.log"))
    foreach ($marker in @(
        "room-route=verified",
        "actor: LeaveGameReq completed. actor=player-x",
        "actor: LeaveGameReq completed. actor=player-o",
        "entry spot: actor destroyed. actor=player-x",
        "entry spot: actor destroyed. actor=player-o"
    )) {
        Wait-LogPattern "TicTacToe lifecycle" $playLogs $marker
    }
    if (-not (Get-ChildItem -Path $env:TICTACTOE_LOG_DIR -Filter "*.log" | Select-String -SimpleMatch "message flow" -Quiet)) {
        throw "TicTacToe framework message-flow evidence was not written"
    }
    if (Get-ChildItem -Path $env:TICTACTOE_LOG_DIR -Filter "*.log" | Select-String -SimpleMatch "message flow phase=error" -Quiet) {
        throw "TicTacToe framework dispatch reported an error"
    }
    Get-Content $clientLog
    Write-Output "PASS TicTacToe.Ts"
}
finally {
    foreach ($process in $processes) {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue }
    }
    if ($redisContainer) {
        try { Invoke-Docker -Arguments @("rm", "-f", "-v", $redisContainer) | Out-Null } catch {}
    }
    if ($runDir -and $env:TICTACTOE_TS_KEEP_RUN_DIR -ne "1") {
        Remove-Item -Recurse -Force $runDir -ErrorAction SilentlyContinue
    }
    elseif ($runDir) { Write-Output "runDir=$runDir" }
    Pop-Location
}
