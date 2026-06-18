Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$runDir = Join-Path ([System.IO.Path]::GetTempPath()) ("zlink-supportchat-ts-" + [System.Guid]::NewGuid().ToString("N"))
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
const required = new Set(['supportchat.api', 'supportchat.support', 'supportchat.notifications']);
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
    $env:SUPPORTCHAT_REGISTRY_PUB_ENDPOINT = if ($env:SUPPORTCHAT_REGISTRY_PUB_ENDPOINT) { $env:SUPPORTCHAT_REGISTRY_PUB_ENDPOINT } else { "tcp://127.0.0.1:$($ports[0])" }
    $env:SUPPORTCHAT_REGISTRY_ROUTER_ENDPOINT = if ($env:SUPPORTCHAT_REGISTRY_ROUTER_ENDPOINT) { $env:SUPPORTCHAT_REGISTRY_ROUTER_ENDPOINT } else { "tcp://127.0.0.1:$($ports[1])" }
    $env:SUPPORTCHAT_SESSION_ENDPOINT = if ($env:SUPPORTCHAT_SESSION_ENDPOINT) { $env:SUPPORTCHAT_SESSION_ENDPOINT } else { "tcp://127.0.0.1:$($ports[2])" }
    $env:SUPPORTCHAT_SUPPORT_ENDPOINT = if ($env:SUPPORTCHAT_SUPPORT_ENDPOINT) { $env:SUPPORTCHAT_SUPPORT_ENDPOINT } else { "tcp://127.0.0.1:$($ports[3])" }
    $env:SUPPORTCHAT_NOTIFICATION_ENDPOINT = if ($env:SUPPORTCHAT_NOTIFICATION_ENDPOINT) { $env:SUPPORTCHAT_NOTIFICATION_ENDPOINT } else { "tcp://127.0.0.1:$($ports[4])" }
    $env:SUPPORTCHAT_API_ENDPOINT = if ($env:SUPPORTCHAT_API_ENDPOINT) { $env:SUPPORTCHAT_API_ENDPOINT } else { "tcp://127.0.0.1:$($ports[5])" }
    $env:ZLINK_SAMPLE_CONFIG = Join-Path $runDir "sample.config.json"

    @{
        sample = @{
            registryPubEndpoint = $env:SUPPORTCHAT_REGISTRY_PUB_ENDPOINT
            registryRouterEndpoint = $env:SUPPORTCHAT_REGISTRY_ROUTER_ENDPOINT
            sessionEndpoint = $env:SUPPORTCHAT_SESSION_ENDPOINT
            supportEndpoint = $env:SUPPORTCHAT_SUPPORT_ENDPOINT
            notificationEndpoint = $env:SUPPORTCHAT_NOTIFICATION_ENDPOINT
            apiEndpoint = $env:SUPPORTCHAT_API_ENDPOINT
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
    Wait-Port "registry-pub" $env:SUPPORTCHAT_REGISTRY_PUB_ENDPOINT
    Wait-Port "registry-router" $env:SUPPORTCHAT_REGISTRY_ROUTER_ENDPOINT

    Start-Server "support" "dist/Server/Support/main.js"
    Wait-Port "support" $env:SUPPORTCHAT_SUPPORT_ENDPOINT
    Wait-Port "notifications" $env:SUPPORTCHAT_NOTIFICATION_ENDPOINT

    Start-Server "api" "dist/Server/Api/main.js"
    Wait-Port "api" $env:SUPPORTCHAT_API_ENDPOINT

    Start-Server "session" "dist/Server/Session/main.js"
    Wait-Port "session" $env:SUPPORTCHAT_SESSION_ENDPOINT
    Wait-DiscoveryReady $env:SUPPORTCHAT_REGISTRY_ROUTER_ENDPOINT

    $clientLog = Join-Path $logDir "client.log"
    node (Join-Path $scriptDir "dist/Client/main.js") *> $clientLog
    if (-not (Select-String -Path $clientLog -Pattern "stream-inbound sample=SupportChat" -Quiet)) {
        throw "SupportChat.Ts client did not write stream-inbound marker."
    }
    if (-not (Select-String -Path $clientLog -Pattern "stream-inbound sample=SupportChat .* seq=[0-9]" -Quiet)) {
        throw "SupportChat.Ts client did not write sequenced stream-inbound response marker."
    }
    if (-not (Select-String -Path $clientLog -Pattern "stream-inbound sample=SupportChat .* name=.*Notify" -Quiet)) {
        throw "SupportChat.Ts client did not write stream-inbound push marker."
    }
}
finally {
    for ($i = $processes.Count - 1; $i -ge 0; $i--) {
        $process = $processes[$i]
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
    }
    if ($env:SUPPORTCHAT_TS_KEEP_RUN_DIR -ne "1") {
        Remove-Item -Recurse -Force $runDir -ErrorAction SilentlyContinue
    }
    else {
        Write-Host "runDir=$runDir"
    }
}
