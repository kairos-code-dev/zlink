Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

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

function Wait-Redis([string]$Container) {
    for ($i = 0; $i -lt 300; $i++) {
        try {
            if ((Invoke-Docker -Arguments @("exec", $Container, "redis-cli", "PING") -TimeoutSeconds 5) -eq "PONG") {
                return
            }
        }
        catch {}
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for Redis container $Container to answer PING."
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $IsWindows) {
    & bash (Join-Path $scriptDir "run_sample.sh")
    exit $LASTEXITCODE
}
$runDir = Join-Path ([System.IO.Path]::GetTempPath()) ("zlink-bingo-ts-" + [System.Guid]::NewGuid().ToString("N"))
$logDir = Join-Path $runDir "logs"
New-Item -ItemType Directory -Path $logDir | Out-Null
$processes = New-Object System.Collections.Generic.List[System.Diagnostics.Process]
$redisContainer = $null
$probe = $null

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

function Use-Default([string]$Value, [string]$Fallback) {
    if ([string]::IsNullOrEmpty($Value)) {
        return $Fallback
    }
    return $Value
}

function Get-EndpointParts([string]$Endpoint) {
    $value = $Endpoint -replace '^tcp://', '' -replace '^ws://', ''
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

function Write-SampleConfig([string]$Path, [hashtable]$Sample) {
    @{ sample = $Sample } | ConvertTo-Json -Depth 8 | Set-Content -Path $Path -Encoding UTF8
}

function Wait-LocationReady(
    [string]$RedisEndpoint,
    [string]$LocationKeyPrefix,
    [string]$ApiAEndpoint,
    [string]$ApiBEndpoint,
    [string]$PlayASpotEndpoint,
    [string]$PlayBSpotEndpoint
) {
    $script = @'
const redisEndpoint = process.argv[2];
const keyPrefix = process.argv[3];
const expectedApiEndpoints = new Set(process.argv.slice(4, 6));
const expectedSpotEndpoints = new Set(process.argv.slice(6));
const framework = require('@zlink-systems/framework');
const { ZLinkRedisLocationStore } = require('@zlink-systems/framework-locations-redis');

const store = new ZLinkRedisLocationStore({
  url: `redis://${redisEndpoint}`,
  keyPrefix
});

async function main() {
  let lastPeers = [];
  let lastSpotPeers = [];
  let lastLeases = [];
  for (let attempt = 0; attempt < 100; attempt += 1) {
    lastPeers = await store.listPeers({
      autoConnectType: framework.ZLinkLocationAutoConnectType.ClientServer,
      meshName: 'bingo.api',
      role: framework.ZLinkLocationRole.Router
    });
    lastSpotPeers = await store.listPeers({
      autoConnectType: framework.ZLinkLocationAutoConnectType.SpotMesh,
      meshName: 'bingo.room',
      role: framework.ZLinkLocationRole.Spot
    });
    lastLeases = (await store.listOwnerLeases()).leases;
    if ([...expectedApiEndpoints].every((endpoint) =>
      lastPeers.some((peer) => peer.endpoint === endpoint && lastLeases.some((lease) => lease.ownerId === peer.ownerId))) &&
      [...expectedSpotEndpoints].every((endpoint) =>
        lastSpotPeers.some((peer) => peer.endpoint === endpoint && lastLeases.some((lease) => lease.ownerId === peer.ownerId)))) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  console.error('Timed out waiting for Bingo location readiness.');
  console.error(JSON.stringify(
    { peers: lastPeers, spotPeers: lastSpotPeers, leases: lastLeases },
    (_key, value) => typeof value === 'bigint' ? value.toString() : value,
    2
  ));
  process.exit(1);
}

main()
  .finally(() => store.dispose())
  .catch((error) => {
    console.error(error);
    process.exit(1);
  });
'@
    $script | node - $RedisEndpoint $LocationKeyPrefix $ApiAEndpoint $ApiBEndpoint $PlayASpotEndpoint $PlayBSpotEndpoint
    if ($LASTEXITCODE -ne 0) {
        throw "Timed out waiting for Bingo location readiness."
    }
}

function Start-Server([string]$Name, [string]$Entry, [string]$ConfigPath) {
    $environment = @{
        ZLINK_SAMPLE_CONFIG = $ConfigPath
    }
    $process = Start-Process -FilePath "node" `
        -ArgumentList @((Join-Path $scriptDir $Entry)) `
        -WorkingDirectory $scriptDir `
        -RedirectStandardOutput (Join-Path $logDir "$Name.log") `
        -RedirectStandardError (Join-Path $logDir "$Name.err.log") `
        -Environment $environment `
        -PassThru
    $processes.Add($process)
}

function Start-ServerAndWait([string]$Name, [string]$Entry, [string]$ConfigPath, [hashtable]$Endpoints) {
    Start-Server $Name $Entry $ConfigPath
    foreach ($endpointName in $Endpoints.Keys) {
        Wait-Port $endpointName $Endpoints[$endpointName]
    }
}

function Assert-LogContains([string]$Path, [string]$Pattern, [string]$Message) {
    if (-not (Select-String -Path $Path -Pattern $Pattern -Quiet)) {
        throw $Message
    }
}

function Assert-LogCount([string]$Pattern, [int]$Expected, [string]$Message) {
    $count = @(Get-ChildItem -Path $logDir -Filter "*.log" | Select-String -Pattern $Pattern).Count
    if ($count -ne $Expected) { throw "$Message Expected=$Expected Actual=$count" }
}

try {
    Push-Location $scriptDir
    try {
        npm run build | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "Bingo.Ts build failed." }
    }
    finally {
        Pop-Location
    }
    $env:BINGO_LOG_DIR = $logDir
    $ports = Get-FreePorts 18
    $sessionAEndpoint = Use-Default $env:BINGO_SESSION_A_ENDPOINT "ws://127.0.0.1:$($ports[2])"
    $sessionARouteEndpoint = Use-Default $env:BINGO_SESSION_A_ROUTE_ENDPOINT "tcp://127.0.0.1:$($ports[3])"
    $sessionASpotEndpoint = Use-Default $env:BINGO_SESSION_A_SPOT_ENDPOINT "tcp://127.0.0.1:$($ports[4])"
    $sessionASpotNodeRid = Use-Default $env:BINGO_SESSION_A_SPOT_NODE_RID "bingo-session-node-a"
    $sessionBEndpoint = Use-Default $env:BINGO_SESSION_B_ENDPOINT "ws://127.0.0.1:$($ports[5])"
    $sessionBRouteEndpoint = Use-Default $env:BINGO_SESSION_B_ROUTE_ENDPOINT "tcp://127.0.0.1:$($ports[6])"
    $sessionBSpotEndpoint = Use-Default $env:BINGO_SESSION_B_SPOT_ENDPOINT "tcp://127.0.0.1:$($ports[7])"
    $sessionBSpotNodeRid = Use-Default $env:BINGO_SESSION_B_SPOT_NODE_RID "bingo-session-node-b"
    $playAEndpoint = Use-Default $env:BINGO_PLAY_A_ENDPOINT "tcp://127.0.0.1:$($ports[8])"
    $playARouteEndpoint = Use-Default $env:BINGO_PLAY_A_ROUTE_ENDPOINT "tcp://127.0.0.1:$($ports[9])"
    $playASpotEndpoint = Use-Default $env:BINGO_PLAY_A_SPOT_ENDPOINT "tcp://127.0.0.1:$($ports[10])"
    $playASpotPubSubEndpoint = Use-Default $env:BINGO_PLAY_A_SPOT_PUBSUB_ENDPOINT "tcp://127.0.0.1:$($ports[11])"
    $playASpotNodeRid = Use-Default $env:BINGO_PLAY_A_SPOT_NODE_RID "bingo-play-node-a"
    $playBEndpoint = Use-Default $env:BINGO_PLAY_B_ENDPOINT "tcp://127.0.0.1:$($ports[12])"
    $playBRouteEndpoint = Use-Default $env:BINGO_PLAY_B_ROUTE_ENDPOINT "tcp://127.0.0.1:$($ports[13])"
    $playBSpotEndpoint = Use-Default $env:BINGO_PLAY_B_SPOT_ENDPOINT "tcp://127.0.0.1:$($ports[14])"
    $playBSpotPubSubEndpoint = Use-Default $env:BINGO_PLAY_B_SPOT_PUBSUB_ENDPOINT "tcp://127.0.0.1:$($ports[15])"
    $playBSpotNodeRid = Use-Default $env:BINGO_PLAY_B_SPOT_NODE_RID "bingo-play-node-b"
    $apiAEndpoint = Use-Default $env:BINGO_API_A_ENDPOINT "tcp://127.0.0.1:$($ports[16])"
    $apiBEndpoint = Use-Default $env:BINGO_API_B_ENDPOINT "tcp://127.0.0.1:$($ports[17])"
    $redisKeyPrefix = Use-Default $env:BINGO_REDIS_KEY_PREFIX "bingo:node:${PID}:$([Guid]::NewGuid().ToString('N')):"

    # The sample owns its Redis: always provision a dedicated, throwaway container
    # so room-allocation state stays isolated per run and never touches a developer's
    # local Redis. (BINGO_REDIS_ENDPOINT is intentionally derived here, not read.)
    if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
        throw "Docker is required to run the Bingo sample (it provisions a dedicated Redis container)."
    }
    $redisName = "zlink-redis-node-bingo-$PID-$([Guid]::NewGuid().ToString('N'))"
    $redisImage = Use-Default $env:ZLINK_REDIS_IMAGE "redis:7.2-alpine"
    $redisContainer = Invoke-Docker -Arguments @("create", "--name", $redisName, "--tmpfs", "/data", "-p", "127.0.0.1::6379", $redisImage)
    if ([string]::IsNullOrWhiteSpace($redisContainer)) { throw "Failed to create Redis Docker container." }
    Invoke-Docker -Arguments @("start", $redisContainer) | Out-Null
    $running = Invoke-Docker -Arguments @("inspect", "-f", "{{.State.Running}}", $redisContainer)
    if ($running -ne 'true') { throw "Redis Docker container is not running." }
    $redisPort = Invoke-Docker -Arguments @(
        "inspect", "-f", "{{(index (index .NetworkSettings.Ports `"6379/tcp`") 0).HostPort}}", $redisContainer
    )
    if ($redisPort -notmatch '^\d+$') { throw "Failed to inspect Redis Docker host port." }
    $redisEndpoint = "127.0.0.1:$redisPort"
    Wait-Redis $redisContainer

    $clientConfig = Join-Path $runDir "client.config.json"
    $apiAConfig = Join-Path $runDir "api-a.config.json"
    $apiBConfig = Join-Path $runDir "api-b.config.json"
    $playAConfig = Join-Path $runDir "play-a.config.json"
    $playBConfig = Join-Path $runDir "play-b.config.json"
    $sessionAConfig = Join-Path $runDir "session-a.config.json"
    $sessionBConfig = Join-Path $runDir "session-b.config.json"

    $base = @{
        redisEndpoint = $redisEndpoint
        redisKeyPrefix = $redisKeyPrefix
    }
    Write-SampleConfig $clientConfig ($base + @{
        sessionAEndpoint = $sessionAEndpoint
        sessionBEndpoint = $sessionBEndpoint
    })
    Write-SampleConfig $apiAConfig ($base + @{
        apiEndpoint = $apiAEndpoint
    })
    Write-SampleConfig $apiBConfig ($base + @{
        apiEndpoint = $apiBEndpoint
    })
    Write-SampleConfig $playAConfig ($base + @{
        playEndpoint = $playAEndpoint
        playRouteEndpoint = $playARouteEndpoint
        playSpotEndpoint = $playASpotEndpoint
        playSpotPubSubEndpoint = $playASpotPubSubEndpoint
        playSpotNodeRid = $playASpotNodeRid
    })
    Write-SampleConfig $playBConfig ($base + @{
        playEndpoint = $playBEndpoint
        playRouteEndpoint = $playBRouteEndpoint
        playSpotEndpoint = $playBSpotEndpoint
        playSpotPubSubEndpoint = $playBSpotPubSubEndpoint
        playSpotNodeRid = $playBSpotNodeRid
    })
    Write-SampleConfig $sessionAConfig ($base + @{
        sessionEndpoint = $sessionAEndpoint
        sessionRouteEndpoint = $sessionARouteEndpoint
        sessionSpotEndpoint = $sessionASpotEndpoint
        sessionSpotNodeRid = $sessionASpotNodeRid
        preferredPlayNodeRid = $playASpotNodeRid
    })
    Write-SampleConfig $sessionBConfig ($base + @{
        sessionEndpoint = $sessionBEndpoint
        sessionRouteEndpoint = $sessionBRouteEndpoint
        sessionSpotEndpoint = $sessionBSpotEndpoint
        sessionSpotNodeRid = $sessionBSpotNodeRid
        preferredPlayNodeRid = $playBSpotNodeRid
    })

    Wait-Port "redis" "tcp://$redisEndpoint"

    Start-ServerAndWait "api-a" "dist/Server/Api/main.js" $apiAConfig @{
        "api-a" = $apiAEndpoint
    }

    Start-ServerAndWait "api-b" "dist/Server/Api/main.js" $apiBConfig @{
        "api-b" = $apiBEndpoint
    }

    Start-ServerAndWait "play-a" "dist/Server/Play/main.js" $playAConfig @{
        "play-a" = $playAEndpoint
        "play-a-route" = $playARouteEndpoint
        "play-a-spot" = $playASpotEndpoint
        "play-a-spot-pubsub" = $playASpotPubSubEndpoint
    }
    $playA = $processes[$processes.Count - 1]

    Start-ServerAndWait "play-b" "dist/Server/Play/main.js" $playBConfig @{
        "play-b" = $playBEndpoint
        "play-b-route" = $playBRouteEndpoint
        "play-b-spot" = $playBSpotEndpoint
        "play-b-spot-pubsub" = $playBSpotPubSubEndpoint
    }

    Start-ServerAndWait "session-a" "dist/Server/Session/main.js" $sessionAConfig @{
        "session-a" = $sessionAEndpoint
        "session-a-route" = $sessionARouteEndpoint
        "session-a-spot" = $sessionASpotEndpoint
    }

    Start-ServerAndWait "session-b" "dist/Server/Session/main.js" $sessionBConfig @{
        "session-b" = $sessionBEndpoint
        "session-b-route" = $sessionBRouteEndpoint
        "session-b-spot" = $sessionBSpotEndpoint
    }
    Wait-LocationReady "$redisEndpoint" "$($redisKeyPrefix)location" $apiAEndpoint $apiBEndpoint $playASpotEndpoint $playBSpotEndpoint

    $probeLog = Join-Path $logDir "drain-probe.log"
    $probeGate = Join-Path $runDir "drain-probe.gate"
    $probe = Start-Process -FilePath "node" `
        -ArgumentList @((Join-Path $scriptDir "../../scripts/browser-e2e/run-sample.mjs"), "Bingo.Ts", "drain-match-probe.ts") `
        -WorkingDirectory $scriptDir `
        -RedirectStandardOutput $probeLog `
        -RedirectStandardError (Join-Path $logDir "drain-probe.err.log") `
        -Environment @{
            ZLINK_SAMPLE_CONFIG = $clientConfig
            BINGO_DRAIN_EXCLUDED_NODE_RID = $playASpotNodeRid
            BINGO_DRAIN_GATE_FILE = $probeGate
        } `
        -PassThru
    $probeReadyDeadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        $probeReady = Select-String -Path $probeLog -SimpleMatch "bingo-drain-probe ready" -Quiet
        if (-not $probeReady -and -not $probe.HasExited) { Start-Sleep -Milliseconds 50 }
    } while (-not $probeReady -and -not $probe.HasExited -and [DateTime]::UtcNow -lt $probeReadyDeadline)
    if (-not $probeReady) { throw "Drain matching probe did not authenticate before drain." }

    $clientLog = Join-Path $logDir "client.log"
    $client = Start-Process -FilePath "node" `
        -ArgumentList @((Join-Path $scriptDir "../../scripts/browser-e2e/run-sample.mjs"), "Bingo.Ts") `
        -WorkingDirectory $scriptDir `
        -RedirectStandardOutput $clientLog `
        -RedirectStandardError (Join-Path $logDir "client.err.log") `
        -PassThru
    $timerDeadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        $timerStarted = Get-ChildItem -Path $logDir -Filter "*.log" | Select-String -Pattern "bingo-lifecycle timer-started" -Quiet
        if (-not $timerStarted -and -not $client.HasExited) { Start-Sleep -Milliseconds 50 }
    } while (-not $timerStarted -and -not $client.HasExited -and [DateTime]::UtcNow -lt $timerDeadline)
    if (-not $timerStarted) { throw "Bingo.Ts did not reach the drain checkpoint." }
    & node -e "process.kill($($playA.Id), 'SIGBREAK')"
    if ($LASTEXITCODE -ne 0) { throw "Failed to signal play-a drain." }
    $drainingDeadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        $draining = Select-String -Path (Join-Path $logDir "play-a.log") `
            -SimpleMatch 'zlink metric name=zlink.drain.state value=1 attributes={"state":"draining"}' `
            -Quiet
        if (-not $draining) { Start-Sleep -Milliseconds 50 }
    } while (-not $draining -and [DateTime]::UtcNow -lt $drainingDeadline)
    if (-not $draining) { throw "play-a did not publish its draining state." }
    Set-Content -Path $probeGate -Value "draining"
    $probe.WaitForExit()
    if ($probe.ExitCode -ne 0) { throw "Drain matching probe exited with $($probe.ExitCode)." }
    Assert-LogContains $probeLog "bingo-drain-probe .* excluded=$playASpotNodeRid" "Drain probe did not exclude play-a."
    $client.WaitForExit()
    if ($client.ExitCode -ne 0) {
        throw "Bingo.Ts client exited with $($client.ExitCode)."
    }
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    $requiredLifecyclePatterns = @(
        "bingo-lifecycle session-disconnect actor=observer destroy=false",
        "bingo-lifecycle session-disconnect actor=player-1 destroy=false",
        "bingo-lifecycle session-disconnect actor=player-2 destroy=false",
        "bingo-lifecycle entry-destroy-complete actor=player-1",
        "bingo-lifecycle entry-destroy-complete actor=player-2"
    )
    do {
        $lifecycleReady = $true
        foreach ($pattern in $requiredLifecyclePatterns) {
            if (-not (Get-ChildItem -Path $logDir -Filter "*.log" | Select-String -Pattern $pattern -Quiet)) {
                $lifecycleReady = $false
                break
            }
        }
        if (-not $lifecycleReady) { Start-Sleep -Milliseconds 50 }
    } while (-not $lifecycleReady -and [DateTime]::UtcNow -lt $deadline)
    Assert-LogContains $clientLog "stream-inbound sample=Bingo" "Bingo.Ts client did not write stream-inbound marker."
    Assert-LogContains $clientLog "stream-inbound sample=Bingo .* seq=[0-9]" "Bingo.Ts client did not write sequenced stream-inbound response marker."
    Assert-LogContains $clientLog "stream-inbound sample=Bingo .* name=.*Notify" "Bingo.Ts client did not write stream-inbound push marker."
    Assert-LogContains $clientLog "client=player-1" "Bingo.Ts client did not write player-1 inbound marker."
    Assert-LogContains $clientLog "client=player-2" "Bingo.Ts client did not write player-2 inbound marker."
    Assert-LogContains $clientLog "client=observer" "Bingo.Ts client did not write observer inbound marker."
    Assert-LogContains $clientLog "name=BingoRewardAnnouncedNotify" "Bingo.Ts client did not observe reward push."
    if (-not (Get-ChildItem -Path $logDir -Filter "*.log" -ErrorAction SilentlyContinue | Select-String -Pattern "message flow" -Quiet)) {
        throw "Bingo.Ts did not write message flow logs."
    }
    if (-not (Get-ChildItem -Path $logDir -Filter "*.log" | Select-String -Pattern "origin=Timer" -Quiet)) {
        throw "Bingo.Ts did not write timer-origin flow evidence."
    }
    & node (Join-Path $scriptDir "scripts/verify-flow-evidence.js") $logDir
    if ($LASTEXITCODE -ne 0) { throw "Bingo.Ts flow continuity validation failed." }
    Assert-LogContains (Join-Path $logDir "play-a.log") "bingo-drain result=drained" "play-a did not drain cleanly."
    Assert-LogContains (Join-Path $logDir "play-a.log") "zlink metric name=zlink.drain.actors.handed_off" "play-a did not hand off an actor during drain."
    if (-not (Get-ChildItem -Path $logDir -Filter "*.log" | Select-String -Pattern "zlink metric name=zlink.stream.connections.active" -Quiet)) {
        throw "Bingo.Ts did not export stream connection metrics."
    }
    if (-not (Get-ChildItem -Path $logDir -Filter "*.log" | Select-String -Pattern 'zlink metric name=zlink.spot.queue.depth .*attributes=.*"kind":"user"' -Quiet)) {
        throw "Bingo.Ts did not export user queue depth metrics."
    }
    if (-not (Get-ChildItem -Path $logDir -Filter "*.log" | Select-String -Pattern "zlink metric name=zlink.actor.transfers" -Quiet)) {
        throw "Bingo.Ts did not export actor transfer metrics."
    }
    Assert-LogCount "bingo-lifecycle timer-started" 1 "Bingo draw timer did not start exactly once."
    foreach ($actor in @("player-1", "player-2")) {
        Assert-LogCount "bingo-lifecycle room-leave actor=$actor" 1 "Room leave evidence is invalid for $actor."
        Assert-LogCount "bingo-lifecycle entry-leave actor=$actor" 1 "Entry leave evidence is invalid for $actor."
        Assert-LogCount "bingo-lifecycle entry-destroy-complete actor=$actor" 1 "Destroy evidence is invalid for $actor."
        Assert-LogCount "bingo-lifecycle session-disconnect actor=$actor destroy=false" 1 "Disconnect evidence is invalid for $actor."
    }
    Assert-LogCount "bingo-lifecycle room-leave actor=observer" 1 "Observer leave evidence is invalid."
    if (-not (Get-ChildItem -Path $logDir -Filter "*.log" | Select-String -Pattern "bingo-lifecycle entry-joined actor=observer destroy=false" -Quiet)) {
        throw "Observer did not rejoin its Entry Spot."
    }
    Assert-LogCount "bingo-lifecycle session-disconnect actor=observer destroy=false" 1 "Observer disconnect evidence is invalid."
    if (Get-ChildItem -Path $logDir -Filter "*.log" | Select-String -Pattern "handlerException|phase=error|dispatch error" -Quiet) {
        throw "Bingo.Ts emitted a dispatch error."
    }
    Write-Host "bingo=completed"
    Write-Host "PASS Bingo.Ts"
}
finally {
    if ($null -ne $probe -and -not $probe.HasExited) {
        Stop-Process -Id $probe.Id -Force -ErrorAction SilentlyContinue
    }
    for ($i = $processes.Count - 1; $i -ge 0; $i--) {
        $process = $processes[$i]
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
    }
    if ($redisContainer) {
        try { Invoke-Docker -Arguments @("rm", "-f", "-v", $redisContainer) | Out-Null } catch {}
    }
    if ($env:BINGO_TS_KEEP_RUN_DIR -ne "1") {
        Remove-Item -Recurse -Force $runDir -ErrorAction SilentlyContinue
    }
    else {
        Write-Host "runDir=$runDir"
    }
}
