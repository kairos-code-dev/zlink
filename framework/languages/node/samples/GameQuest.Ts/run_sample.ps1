Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $IsWindows) {
    & bash (Join-Path $scriptDir "run_sample.sh")
    exit $LASTEXITCODE
}
Push-Location $scriptDir
$processes = @()
$runDir = Join-Path ([System.IO.Path]::GetTempPath()) ("gamequest-" + [System.Guid]::NewGuid().ToString("N"))

try {
    npm run build | Out-Null
    New-Item -ItemType Directory -Force -Path $runDir | Out-Null
    $env:GAMEQUEST_LOG_DIR = if ($env:GAMEQUEST_LOG_DIR) { $env:GAMEQUEST_LOG_DIR } else { Join-Path $scriptDir "logs" }
    $env:GAMEQUEST_STORE_DIR = Join-Path $runDir "store"
    New-Item -ItemType Directory -Force -Path $env:GAMEQUEST_LOG_DIR | Out-Null
    Get-ChildItem -Path $env:GAMEQUEST_LOG_DIR -Filter "*.log" -ErrorAction SilentlyContinue | Remove-Item -Force

    $registryPubPort = New-FreePort
    $registryRouterPort = New-FreePort
    $apiAPort = New-FreePort
    $apiBPort = New-FreePort
    $apiAStreamPort = New-FreePort
    $apiBStreamPort = New-FreePort
    $missionAPort = New-FreePort
    $missionBPort = New-FreePort

    $env:GAMEQUEST_REGISTRY_PUB_ENDPOINT = "tcp://127.0.0.1:$registryPubPort"
    $env:GAMEQUEST_REGISTRY_ROUTER_ENDPOINT = "tcp://127.0.0.1:$registryRouterPort"
    $env:GAMEQUEST_API_A_HTTP = "http://127.0.0.1:$apiAPort"
    $env:GAMEQUEST_API_B_HTTP = "http://127.0.0.1:$apiBPort"
    $env:GAMEQUEST_API_A_STREAM = "tcp://127.0.0.1:$apiAStreamPort"
    $env:GAMEQUEST_API_B_STREAM = "tcp://127.0.0.1:$apiBStreamPort"
    $env:GAMEQUEST_MISSION_A_ENDPOINT = "tcp://127.0.0.1:$missionAPort"
    $env:GAMEQUEST_MISSION_B_ENDPOINT = "tcp://127.0.0.1:$missionBPort"
    $env:ZLINK_SAMPLE_CONFIG = Join-Path $runDir "sample.env"

    $processes += Start-Role "registry"
    Start-Sleep -Milliseconds 500
    $processes += Start-Role "mission-a"
    $processes += Start-Role "mission-b"
    Wait-Port -Port $missionAPort -Name "mission-a"
    Wait-Port -Port $missionBPort -Name "mission-b"
    Wait-Topology
    $processes += Start-Role "api-a"
    $processes += Start-Role "api-b"
    Wait-Port -Port $apiAStreamPort -Name "api-a-stream"
    Wait-Port -Port $apiBStreamPort -Name "api-b-stream"
    Wait-Http -Url $env:GAMEQUEST_API_A_HTTP
    Wait-Http -Url $env:GAMEQUEST_API_B_HTTP

    node (Join-Path $scriptDir "dist/Client/main.js")
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Assert-LogContains -Path (Join-Path $env:GAMEQUEST_LOG_DIR "flow-api-a.log") -Pattern "label=api-a"
    Assert-LogContains -Path (Join-Path $env:GAMEQUEST_LOG_DIR "flow-api-b.log") -Pattern "label=api-b"
    Assert-LogContains -Path (Join-Path $env:GAMEQUEST_LOG_DIR "flow-mission-a.log") -Pattern "label=mission-a"
    Assert-LogContains -Path (Join-Path $env:GAMEQUEST_LOG_DIR "flow-mission-b.log") -Pattern "label=mission-b"
} finally {
    foreach ($process in $processes) {
        if ($null -ne $process -and -not $process.HasExited) {
            Stop-Process -Id $process.Id -Force
        }
    }
    if (Test-Path $runDir) {
        Remove-Item -Recurse -Force $runDir
    }
    Pop-Location
}

function New-FreePort {
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Parse("127.0.0.1"), 0)
    $listener.Start()
    $port = $listener.LocalEndpoint.Port
    $listener.Stop()
    return $port
}

function Start-Role {
    param([string]$Role)
    Start-Process -FilePath "node" `
        -ArgumentList @((Join-Path $scriptDir "dist/Server/main.js"), "--role", $Role) `
        -NoNewWindow `
        -RedirectStandardOutput (Join-Path $runDir "$Role.out.log") `
        -RedirectStandardError (Join-Path $runDir "$Role.err.log") `
        -PassThru
}

function Wait-Port {
    param([int]$Port, [string]$Name)
    $deadline = (Get-Date).AddSeconds(10)
    while ((Get-Date) -lt $deadline) {
        try {
            $client = [System.Net.Sockets.TcpClient]::new()
            $client.Connect("127.0.0.1", $Port)
            $client.Close()
            return
        } catch {
            Start-Sleep -Milliseconds 100
        }
    }
    throw "Timed out waiting for $Name"
}

function Wait-Http {
    param([string]$Url)
    $deadline = (Get-Date).AddSeconds(10)
    while ((Get-Date) -lt $deadline) {
        try {
            $response = Invoke-WebRequest -UseBasicParsing -Uri "$Url/health" -TimeoutSec 1
            if ($response.StatusCode -ge 200 -and $response.StatusCode -lt 300) {
                return
            }
        } catch {
            Start-Sleep -Milliseconds 100
        }
    }
    throw "Timed out waiting for $Url"
}

function Wait-Topology {
    $script = @"
const registryEndpoint = process.env.GAMEQUEST_REGISTRY_ROUTER_ENDPOINT;
const zlink = require('@zlink-systems/zlink');
const pause = new Int32Array(new SharedArrayBuffer(4));
const context = zlink.createContext();
const client = zlink.createRegistryQueryClient(context);
try {
  client.connect(registryEndpoint);
  for (let attempt = 0; attempt < 100; attempt += 1) {
    const ready = client.topology().filter((entry) =>
      entry.channelName === 'gamequest.quest.mission.route' &&
      entry.state === 3 &&
      typeof entry.endpoint === 'string' &&
      entry.endpoint.length > 0);
    if (ready.length >= 2) {
      process.exit(0);
    }
    Atomics.wait(pause, 0, 0, 100);
  }
  process.exit(1);
} finally {
  client.close();
  context.close();
}

function Assert-LogContains {
    param([string]$Path, [string]$Pattern)
    if (-not (Test-Path $Path)) {
        throw "Missing log file $Path"
    }
    if (-not (Select-String -Path $Path -Pattern $Pattern -Quiet)) {
        throw "Log file $Path did not contain $Pattern"
    }
}
"@
    node -e $script
    if ($LASTEXITCODE -ne 0) {
        throw "Timed out waiting for GameQuest discovery readiness."
    }
}
