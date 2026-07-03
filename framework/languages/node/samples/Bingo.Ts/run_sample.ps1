Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

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
    $value = $Endpoint -replace '^tcp://', ''
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

try {
    $ports = Get-FreePorts 18
    $sessionAEndpoint = Use-Default $env:BINGO_SESSION_A_ENDPOINT "tcp://127.0.0.1:$($ports[2])"
    $sessionARouteEndpoint = Use-Default $env:BINGO_SESSION_A_ROUTE_ENDPOINT "tcp://127.0.0.1:$($ports[3])"
    $sessionASpotEndpoint = Use-Default $env:BINGO_SESSION_A_SPOT_ENDPOINT "tcp://127.0.0.1:$($ports[4])"
    $sessionASpotNodeRid = Use-Default $env:BINGO_SESSION_A_SPOT_NODE_RID "bingo-session-node-a"
    $sessionBEndpoint = Use-Default $env:BINGO_SESSION_B_ENDPOINT "tcp://127.0.0.1:$($ports[5])"
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
    $redisContainer = "bingo-node-redis-$PID-$([Guid]::NewGuid().ToString('N'))"
    & docker run -d --rm --name $redisContainer -p "127.0.0.1::6379" redis:7.2-alpine | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to start Redis Docker container."
    }
    $redisPort = (& docker port $redisContainer "6379/tcp") -replace '^.*:', ''
    $redisEndpoint = "127.0.0.1:$redisPort"

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
        apiNodeRid = "bingo-api-node-a"
        playRouteEndpoints = @($playARouteEndpoint, $playBRouteEndpoint)
    })
    Write-SampleConfig $apiBConfig ($base + @{
        apiEndpoint = $apiBEndpoint
        apiNodeRid = "bingo-api-node-b"
        playRouteEndpoints = @($playARouteEndpoint, $playBRouteEndpoint)
    })
    Write-SampleConfig $playAConfig ($base + @{
        playEndpoint = $playAEndpoint
        playRouteEndpoint = $playARouteEndpoint
        routePeerEndpoints = @($playBRouteEndpoint, $sessionARouteEndpoint, $sessionBRouteEndpoint)
        playSpotEndpoint = $playASpotEndpoint
        playSpotPubSubEndpoint = $playASpotPubSubEndpoint
        playSpotNodeRid = $playASpotNodeRid
    })
    Write-SampleConfig $playBConfig ($base + @{
        playEndpoint = $playBEndpoint
        playRouteEndpoint = $playBRouteEndpoint
        routePeerEndpoints = @($playARouteEndpoint, $sessionARouteEndpoint, $sessionBRouteEndpoint)
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
        preferredPlayRouteEndpoint = $playARouteEndpoint
    })
    Write-SampleConfig $sessionBConfig ($base + @{
        sessionEndpoint = $sessionBEndpoint
        sessionRouteEndpoint = $sessionBRouteEndpoint
        sessionSpotEndpoint = $sessionBSpotEndpoint
        sessionSpotNodeRid = $sessionBSpotNodeRid
        preferredPlayNodeRid = $playBSpotNodeRid
        preferredPlayRouteEndpoint = $playBRouteEndpoint
    })

    Push-Location $scriptDir
    try {
        npm run build | Out-Host
    }
    finally {
        Pop-Location
    }

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

    $clientLog = Join-Path $logDir "client.log"
    $clientEnv = @{ ZLINK_SAMPLE_CONFIG = $clientConfig }
    $client = Start-Process -FilePath "node" `
        -ArgumentList @((Join-Path $scriptDir "dist/Client/main.js")) `
        -WorkingDirectory $scriptDir `
        -RedirectStandardOutput $clientLog `
        -RedirectStandardError (Join-Path $logDir "client.err.log") `
        -Environment $clientEnv `
        -Wait `
        -PassThru
    if ($client.ExitCode -ne 0) {
        throw "Bingo.Ts client exited with $($client.ExitCode)."
    }
    Assert-LogContains $clientLog "stream-inbound sample=Bingo" "Bingo.Ts client did not write stream-inbound marker."
    Assert-LogContains $clientLog "stream-inbound sample=Bingo .* seq=[0-9]" "Bingo.Ts client did not write sequenced stream-inbound response marker."
    Assert-LogContains $clientLog "stream-inbound sample=Bingo .* name=.*Notify" "Bingo.Ts client did not write stream-inbound push marker."
    Assert-LogContains $clientLog "client=observer" "Bingo.Ts client did not write observer inbound marker."
    Assert-LogContains $clientLog "name=BingoRewardAnnouncedNotify" "Bingo.Ts client did not observe reward push."
    if (-not (Get-ChildItem -Path (Join-Path $scriptDir "logs") -Filter "*.log" -ErrorAction SilentlyContinue | Select-String -Pattern "message flow" -Quiet)) {
        throw "Bingo.Ts did not write message flow logs."
    }
    Write-Host "bingo=completed"
    Write-Host "PASS Bingo.Ts"
}
finally {
    for ($i = $processes.Count - 1; $i -ge 0; $i--) {
        $process = $processes[$i]
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
    }
    if ($redisContainer) {
        & docker rm -f $redisContainer *> $null
    }
    if ($env:BINGO_TS_KEEP_RUN_DIR -ne "1") {
        Remove-Item -Recurse -Force $runDir -ErrorAction SilentlyContinue
    }
    else {
        Write-Host "runDir=$runDir"
    }
}
