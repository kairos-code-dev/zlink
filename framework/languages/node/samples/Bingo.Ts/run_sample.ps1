Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
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

function Wait-DiscoveryReady([string]$RegistryEndpoint) {
    $script = @'
const registryEndpoint = process.argv[2];
const zlink = require('@zlink-systems/zlink');
const required = new Set(['bingo.api', 'bingo.play', 'bingo.notifications']);
const pause = new Int32Array(new SharedArrayBuffer(4));
const context = zlink.createContext();
const client = zlink.createRegistryQueryClient(context);

try {
  client.connect(registryEndpoint);
  for (let attempt = 0; attempt < 100; attempt += 1) {
    const ready = new Set(client
      .topology()
      .filter((entry) =>
        required.has(entry.channelName) &&
        entry.state === 3 &&
        typeof entry.endpoint === 'string' &&
        entry.endpoint.length > 0)
      .map((entry) => entry.channelName));
    if ([...required].every((channelName) => ready.has(channelName))) {
      process.exit(0);
    }
    Atomics.wait(pause, 0, 0, 100);
  }
  console.error('Timed out waiting for registry discovery readiness.');
  process.exit(1);
} finally {
  client.close();
  context.close();
}
'@
    $script | node - $RegistryEndpoint
    if ($LASTEXITCODE -ne 0) {
        throw "Timed out waiting for registry discovery readiness."
    }
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
    $ports = Get-FreePorts 6
    $env:BINGO_REGISTRY_PUB_ENDPOINT = if ($env:BINGO_REGISTRY_PUB_ENDPOINT) { $env:BINGO_REGISTRY_PUB_ENDPOINT } else { "tcp://127.0.0.1:$($ports[0])" }
    $env:BINGO_REGISTRY_ROUTER_ENDPOINT = if ($env:BINGO_REGISTRY_ROUTER_ENDPOINT) { $env:BINGO_REGISTRY_ROUTER_ENDPOINT } else { "tcp://127.0.0.1:$($ports[1])" }
    $env:BINGO_SESSION_ENDPOINT = if ($env:BINGO_SESSION_ENDPOINT) { $env:BINGO_SESSION_ENDPOINT } else { "tcp://127.0.0.1:$($ports[2])" }
    $env:BINGO_PLAY_ENDPOINT = if ($env:BINGO_PLAY_ENDPOINT) { $env:BINGO_PLAY_ENDPOINT } else { "tcp://127.0.0.1:$($ports[3])" }
    $env:BINGO_NOTIFICATION_ENDPOINT = if ($env:BINGO_NOTIFICATION_ENDPOINT) { $env:BINGO_NOTIFICATION_ENDPOINT } else { "tcp://127.0.0.1:$($ports[4])" }
    $env:BINGO_API_ENDPOINT = if ($env:BINGO_API_ENDPOINT) { $env:BINGO_API_ENDPOINT } else { "tcp://127.0.0.1:$($ports[5])" }
    $env:BINGO_REDIS_KEY_PREFIX = if ($env:BINGO_REDIS_KEY_PREFIX) { $env:BINGO_REDIS_KEY_PREFIX } else { "bingo:node:${PID}:$([Guid]::NewGuid().ToString('N')):" }
    if (-not $env:BINGO_REDIS_ENDPOINT) {
        if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
            throw "Docker is required when BINGO_REDIS_ENDPOINT is not set."
        }
        $redisContainer = "bingo-node-redis-$PID-$([Guid]::NewGuid().ToString('N'))"
        & docker run -d --rm --name $redisContainer -p "127.0.0.1::6379" redis:7.2-alpine | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to start Redis Docker container."
        }
        $redisPort = (& docker port $redisContainer "6379/tcp") -replace '^.*:', ''
        $env:BINGO_REDIS_ENDPOINT = "127.0.0.1:$redisPort"
    }
    $env:ZLINK_SAMPLE_CONFIG = Join-Path $runDir "sample.config.json"

    @{
        sample = @{
            registryPubEndpoint = $env:BINGO_REGISTRY_PUB_ENDPOINT
            registryRouterEndpoint = $env:BINGO_REGISTRY_ROUTER_ENDPOINT
            sessionEndpoint = $env:BINGO_SESSION_ENDPOINT
            playEndpoint = $env:BINGO_PLAY_ENDPOINT
            notificationEndpoint = $env:BINGO_NOTIFICATION_ENDPOINT
            apiEndpoint = $env:BINGO_API_ENDPOINT
            redisEndpoint = $env:BINGO_REDIS_ENDPOINT
            redisKeyPrefix = $env:BINGO_REDIS_KEY_PREFIX
        }
    } | ConvertTo-Json -Depth 8 | Set-Content -Path $env:ZLINK_SAMPLE_CONFIG -Encoding UTF8

    Push-Location $scriptDir
    try {
        npm run build | Out-Host
    }
    finally {
        Pop-Location
    }

    Start-Server "registry" "dist/Server/Registry/main.js"
    Wait-Port "registry-pub" $env:BINGO_REGISTRY_PUB_ENDPOINT
    Wait-Port "registry-router" $env:BINGO_REGISTRY_ROUTER_ENDPOINT
    Wait-Port "redis" "tcp://$env:BINGO_REDIS_ENDPOINT"

    Start-Server "play" "dist/Server/Play/main.js"
    Wait-Port "play" $env:BINGO_PLAY_ENDPOINT
    Wait-Port "notifications" $env:BINGO_NOTIFICATION_ENDPOINT

    Start-Server "api" "dist/Server/Api/main.js"
    Wait-Port "api" $env:BINGO_API_ENDPOINT

    Start-Server "session" "dist/Server/Session/main.js"
    Wait-Port "session" $env:BINGO_SESSION_ENDPOINT
    Wait-DiscoveryReady $env:BINGO_REGISTRY_ROUTER_ENDPOINT

    $clientLog = Join-Path $logDir "client.log"
    node (Join-Path $scriptDir "dist/Client/main.js") *> $clientLog
    if (-not (Select-String -Path $clientLog -Pattern "stream-inbound sample=Bingo" -Quiet)) {
        throw "Bingo.Ts client did not write stream-inbound marker."
    }
    if (-not (Select-String -Path $clientLog -Pattern "stream-inbound sample=Bingo .* seq=[0-9]" -Quiet)) {
        throw "Bingo.Ts client did not write sequenced stream-inbound response marker."
    }
    if (-not (Select-String -Path $clientLog -Pattern "stream-inbound sample=Bingo .* name=.*Notify" -Quiet)) {
        throw "Bingo.Ts client did not write stream-inbound push marker."
    }
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
