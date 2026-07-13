Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$processes = New-Object System.Collections.Generic.List[System.Diagnostics.Process]
$redisContainer = $null
$ownsRunDir = $false
$runDir = $null

function Invoke-Docker {
  param(
    [string[]] $Arguments,
    [int] $TimeoutSeconds = 10
  )
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
  $output = $process.StandardOutput.ReadToEnd()
  $errorOutput = $process.StandardError.ReadToEnd()
  if ($process.ExitCode -ne 0) {
    throw "docker $($Arguments -join ' ') failed ($($process.ExitCode)): $errorOutput"
  }
  return $output.Trim()
}

function Start-Role {
  param([string] $Name)
  $process = Start-Process -FilePath "node" -ArgumentList @((Join-Path $PSScriptRoot "dist/Server/main.js"), "--role", $Name) -PassThru -RedirectStandardOutput (Join-Path $script:LogDir "$Name.log") -RedirectStandardError (Join-Path $script:LogDir "$Name.err.log")
  $script:processes.Add($process)
  return $process
}

function Wait-Http {
  param([string] $Url)
  for ($i = 0; $i -lt 300; $i++) {
    try {
      Invoke-RestMethod "$Url/health" -TimeoutSec 1 | Out-Null
      return
    } catch {
      Start-Sleep -Milliseconds 100
    }
  }
  throw "Timed out waiting for $Url"
}

function Wait-Tcp {
  param([string] $Endpoint)
  $address = $Endpoint -replace '^tcp://', ''
  $hostName = ($address -split ':')[0]
  $port = [int](($address -split ':')[1])
  for ($i = 0; $i -lt 100; $i++) {
    try {
      $client = [System.Net.Sockets.TcpClient]::new()
      $client.Connect($hostName, $port)
      $client.Close()
      return
    } catch {
      Start-Sleep -Milliseconds 100
    }
  }
  throw "Timed out waiting for $Endpoint"
}

function Get-FreeTcpPorts {
  param([int] $Count)
  $listeners = New-Object System.Collections.Generic.List[System.Net.Sockets.TcpListener]
  try {
    for ($i = 0; $i -lt $Count; $i++) {
      $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
      $listener.Start()
      $listeners.Add($listener)
    }
    return @($listeners | ForEach-Object { ([System.Net.IPEndPoint] $_.LocalEndpoint).Port })
  } finally {
    $listeners | ForEach-Object { $_.Stop() }
  }
}

function Wait-Topology {
  Wait-Http -Url $env:SHOPPINGMALL_WORKFLOW_A_HTTP
  Wait-Tcp -Endpoint $env:SHOPPINGMALL_WORKFLOW_A_CHANNEL_ENDPOINT
  Wait-Tcp -Endpoint $env:SHOPPINGMALL_WORKFLOW_A_SPOT_ENDPOINT
  Wait-Tcp -Endpoint $env:SHOPPINGMALL_WORKFLOW_A_SPOT_PUB_ENDPOINT
  Wait-Http -Url $env:SHOPPINGMALL_WORKFLOW_B_HTTP
  Wait-Tcp -Endpoint $env:SHOPPINGMALL_WORKFLOW_B_CHANNEL_ENDPOINT
  Wait-Tcp -Endpoint $env:SHOPPINGMALL_WORKFLOW_B_SPOT_ENDPOINT
  Wait-Tcp -Endpoint $env:SHOPPINGMALL_WORKFLOW_B_SPOT_PUB_ENDPOINT
  Wait-Http -Url $env:SHOPPINGMALL_API_A_HTTP
  Wait-Http -Url $env:SHOPPINGMALL_API_B_HTTP
}

function Start-Redis {
  $name = "zlink-redis-node-sample-shoppingmall-$([System.Guid]::NewGuid().ToString("N"))"
  try {
    $containerId = Invoke-Docker -Arguments @("create", "--name", $name, "--tmpfs", "/data", "-p", "127.0.0.1::6379", "redis:7.2-alpine")
    if (-not $containerId) { throw "Failed to create Redis container" }
    $script:redisContainer = $containerId
    Invoke-Docker -Arguments @("start", $containerId) | Out-Null
    $running = Invoke-Docker -Arguments @("inspect", "--format", '{{.State.Running}}', $containerId)
    if ($running -ne "true") { throw "Redis container did not enter the running state" }
    $port = Invoke-Docker -Arguments @("inspect", "--format", '{{(index (index .NetworkSettings.Ports "6379/tcp") 0).HostPort}}', $containerId)
    if (-not $port) { throw "Failed to inspect Redis host port" }
    $env:SHOPPINGMALL_REDIS_ENDPOINT = "127.0.0.1:$port"
    for ($i = 0; $i -lt 60; $i++) {
      try {
        if ((Invoke-Docker -Arguments @("exec", $containerId, "redis-cli", "ping") -TimeoutSeconds 5) -eq "PONG") {
          return $containerId
        }
      } catch {
        Start-Sleep -Milliseconds 200
      }
    }
    throw "Timed out waiting for Redis PING"
  } catch {
    if ($script:redisContainer) {
      try { Invoke-Docker -Arguments @("rm", "-f", "-v", $script:redisContainer) | Out-Null } catch {}
      $script:redisContainer = $null
    }
    throw
  }
}

Push-Location $PSScriptRoot
try {
  npm run build | Out-Null
  if ($env:SHOPPINGMALL_RUN_DIR) {
    $runDir = $env:SHOPPINGMALL_RUN_DIR
  } else {
    $runDir = Join-Path ([System.IO.Path]::GetTempPath()) ([System.Guid]::NewGuid().ToString("N"))
    $script:ownsRunDir = $true
  }
  $script:LogDir = Join-Path $runDir "logs"
  $workDir = Join-Path $runDir "work"
  New-Item -ItemType Directory -Force -Path $script:LogDir, $workDir | Out-Null
  $env:SHOPPINGMALL_WORK_DIR = $workDir
  $env:SHOPPINGMALL_LOG_DIR = $script:LogDir
  $ports = Get-FreeTcpPorts -Count 10
  $env:SHOPPINGMALL_API_A_HTTP = "http://127.0.0.1:$($ports[0])"
  $env:SHOPPINGMALL_API_B_HTTP = "http://127.0.0.1:$($ports[1])"
  $env:SHOPPINGMALL_WORKFLOW_A_HTTP = "http://127.0.0.1:$($ports[2])"
  $env:SHOPPINGMALL_WORKFLOW_B_HTTP = "http://127.0.0.1:$($ports[3])"
  $env:SHOPPINGMALL_WORKFLOW_A_CHANNEL_ENDPOINT = "tcp://127.0.0.1:$($ports[4])"
  $env:SHOPPINGMALL_WORKFLOW_B_CHANNEL_ENDPOINT = "tcp://127.0.0.1:$($ports[5])"
  $env:SHOPPINGMALL_WORKFLOW_A_SPOT_ENDPOINT = "tcp://127.0.0.1:$($ports[6])"
  $env:SHOPPINGMALL_WORKFLOW_B_SPOT_ENDPOINT = "tcp://127.0.0.1:$($ports[7])"
  $env:SHOPPINGMALL_WORKFLOW_A_SPOT_PUB_ENDPOINT = "tcp://127.0.0.1:$($ports[8])"
  $env:SHOPPINGMALL_WORKFLOW_B_SPOT_PUB_ENDPOINT = "tcp://127.0.0.1:$($ports[9])"
  $env:SHOPPINGMALL_WORKFLOW_A_ENDPOINT = $env:SHOPPINGMALL_WORKFLOW_A_CHANNEL_ENDPOINT
  $env:SHOPPINGMALL_WORKFLOW_B_ENDPOINT = $env:SHOPPINGMALL_WORKFLOW_B_CHANNEL_ENDPOINT
  $env:SHOPPINGMALL_REDIS_KEY_PREFIX = "shoppingmall:node:ps1:$([System.Guid]::NewGuid().ToString("N")):"
  $redisContainer = Start-Redis

  @("workflow-a", "workflow-b", "api-a", "api-b") | ForEach-Object { Start-Role $_ | Out-Null }
  try {
    Wait-Topology
    node (Join-Path $PSScriptRoot "dist/Client/main.js")
  } finally {
    $processes | ForEach-Object { Stop-Process -Id $_.Id -ErrorAction SilentlyContinue }
  }
} finally {
  if ($redisContainer) {
    try { Invoke-Docker -Arguments @("rm", "-f", "-v", $redisContainer) | Out-Null } catch {}
  }
  if ($ownsRunDir -and $runDir) {
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $runDir
  }
  Pop-Location
}
