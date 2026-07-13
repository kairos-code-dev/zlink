Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$processes = New-Object System.Collections.Generic.List[System.Diagnostics.Process]
$redisContainer = $null

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
      Invoke-RestMethod "$Url/health" | Out-Null
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

function Wait-Topology {
  Wait-Http -Url $env:SHOPPINGMALL_WORKFLOW_A_HTTP
  Wait-Tcp -Endpoint $env:SHOPPINGMALL_WORKFLOW_A_CHANNEL_ENDPOINT
  Wait-Tcp -Endpoint $env:SHOPPINGMALL_WORKFLOW_A_SPOT_ENDPOINT
  Wait-Http -Url $env:SHOPPINGMALL_WORKFLOW_B_HTTP
  Wait-Tcp -Endpoint $env:SHOPPINGMALL_WORKFLOW_B_CHANNEL_ENDPOINT
  Wait-Tcp -Endpoint $env:SHOPPINGMALL_WORKFLOW_B_SPOT_ENDPOINT
  Wait-Http -Url $env:SHOPPINGMALL_API_A_HTTP
  Wait-Http -Url $env:SHOPPINGMALL_API_B_HTTP
}

function Start-Redis {
  $name = "shoppingmall-node-redis-ps1-$([System.Guid]::NewGuid().ToString("N"))"
  $containerId = (& docker run -d --rm --tmpfs /data --name $name -p "127.0.0.1::6379" redis:7.2-alpine).Trim()
  if (-not $containerId) {
    throw "Failed to start Redis container"
  }
  $portLine = (& docker port $containerId "6379/tcp").Trim()
  $port = ($portLine -split ':')[-1]
  $env:SHOPPINGMALL_REDIS_ENDPOINT = "127.0.0.1:$port"
  Wait-Tcp -Endpoint "tcp://$env:SHOPPINGMALL_REDIS_ENDPOINT"
  return $containerId
}

Push-Location $PSScriptRoot
try {
  npm run build | Out-Null
  $runDir = if ($env:SHOPPINGMALL_RUN_DIR) { $env:SHOPPINGMALL_RUN_DIR } else { Join-Path ([System.IO.Path]::GetTempPath()) ([System.Guid]::NewGuid().ToString("N")) }
  $script:LogDir = Join-Path $runDir "logs"
  $workDir = Join-Path $runDir "work"
  New-Item -ItemType Directory -Force -Path $script:LogDir, $workDir | Out-Null
  $env:SHOPPINGMALL_WORK_DIR = $workDir
  $env:SHOPPINGMALL_LOG_DIR = $script:LogDir
  $env:SHOPPINGMALL_API_A_HTTP = "http://127.0.0.1:31401"
  $env:SHOPPINGMALL_API_B_HTTP = "http://127.0.0.1:31402"
  $env:SHOPPINGMALL_WORKFLOW_A_HTTP = "http://127.0.0.1:31403"
  $env:SHOPPINGMALL_WORKFLOW_B_HTTP = "http://127.0.0.1:31404"
  $env:SHOPPINGMALL_WORKFLOW_A_CHANNEL_ENDPOINT = "tcp://127.0.0.1:31405"
  $env:SHOPPINGMALL_WORKFLOW_B_CHANNEL_ENDPOINT = "tcp://127.0.0.1:31406"
  $env:SHOPPINGMALL_WORKFLOW_A_SPOT_ENDPOINT = "tcp://127.0.0.1:31407"
  $env:SHOPPINGMALL_WORKFLOW_B_SPOT_ENDPOINT = "tcp://127.0.0.1:31408"
  $env:SHOPPINGMALL_WORKFLOW_A_ENDPOINT = $env:SHOPPINGMALL_WORKFLOW_A_CHANNEL_ENDPOINT
  $env:SHOPPINGMALL_WORKFLOW_B_ENDPOINT = $env:SHOPPINGMALL_WORKFLOW_B_CHANNEL_ENDPOINT
  $env:SHOPPINGMALL_REDIS_KEY_PREFIX = "shoppingmall:node:ps1:"
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
    docker rm -f $redisContainer | Out-Null
  }
  Pop-Location
}
