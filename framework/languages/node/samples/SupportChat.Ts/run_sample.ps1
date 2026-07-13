Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$processes = New-Object System.Collections.Generic.List[System.Diagnostics.Process]
$redisContainer = $null

function Start-Server {
  param([string] $Name, [string] $Path)
  $process = Start-Process -FilePath "node" -ArgumentList @($Path) -PassThru -RedirectStandardOutput (Join-Path $script:LogDir "$Name.log") -RedirectStandardError (Join-Path $script:LogDir "$Name.err.log")
  $script:processes.Add($process)
  return $process
}

function Wait-Port {
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

function Start-Redis {
  $name = "supportchat-node-redis-ps1-$([System.Guid]::NewGuid().ToString("N"))"
  $containerId = (& docker run -d --rm --tmpfs /data --name $name -p "127.0.0.1::6379" redis:7.2-alpine).Trim()
  if (-not $containerId) {
    throw "Failed to start Redis container"
  }
  $portLine = (& docker port $containerId "6379/tcp").Trim()
  $port = ($portLine -split ':')[-1]
  $env:SUPPORTCHAT_REDIS_ENDPOINT = "127.0.0.1:$port"
  Wait-Tcp -Endpoint "tcp://$env:SUPPORTCHAT_REDIS_ENDPOINT"
  return $containerId
}

Push-Location $PSScriptRoot
try {
  npm run build | Out-Null
  $runDir = if ($env:SUPPORTCHAT_RUN_DIR) { $env:SUPPORTCHAT_RUN_DIR } else { Join-Path ([System.IO.Path]::GetTempPath()) ([System.Guid]::NewGuid().ToString("N")) }
  $script:LogDir = Join-Path $runDir "logs"
  $workDir = Join-Path $runDir "work"
  New-Item -ItemType Directory -Force -Path $script:LogDir, $workDir | Out-Null
  $env:SUPPORTCHAT_WORK_DIR = $workDir
  $env:SUPPORTCHAT_STATE_FILE = Join-Path $workDir "supportchat-state.json"
  $env:SUPPORTCHAT_API_HTTP = "http://127.0.0.1:31180"
  $env:SUPPORTCHAT_API_CHANNEL_ENDPOINT = "tcp://127.0.0.1:31181"
  $env:SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT = "tcp://127.0.0.1:31182"
  $env:SUPPORTCHAT_STREAM_ENDPOINT = "tcp://127.0.0.1:31183"
  $env:SUPPORTCHAT_REDIS_KEY_PREFIX = "supportchat:node:ps1:"
  $redisContainer = Start-Redis

  Start-Server -Name "support" -Path (Join-Path $PSScriptRoot "dist/Server/Support/main.js") | Out-Null
  Start-Server -Name "session" -Path (Join-Path $PSScriptRoot "dist/Server/Session/main.js") | Out-Null
  Start-Server -Name "api" -Path (Join-Path $PSScriptRoot "dist/Server/Api/main.js") | Out-Null
  try {
    Wait-Port -Url $env:SUPPORTCHAT_API_HTTP
    node (Join-Path $PSScriptRoot "dist/Server/Probe/main.js")
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
