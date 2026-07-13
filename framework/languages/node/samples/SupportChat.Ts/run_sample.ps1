Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

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

function Get-FreeTcpPort {
  $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
  $listener.Start()
  try { return ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port } finally { $listener.Stop() }
}

function Start-Server {
  param([string] $Name, [string] $Path)
  $process = Start-Process -FilePath "node" -ArgumentList @($Path) -PassThru `
    -RedirectStandardOutput (Join-Path $script:logDir "$Name.log") `
    -RedirectStandardError (Join-Path $script:logDir "$Name.err.log")
  $script:processes.Add($process)
}

function Wait-Tcp {
  param([string] $Name, [string] $Endpoint)
  $address = $Endpoint -replace '^tcp://', '' -replace '^ws://', ''
  $hostName, $portText = $address -split ':'
  for ($i = 0; $i -lt 300; $i++) {
    try {
      $client = [System.Net.Sockets.TcpClient]::new()
      $client.Connect($hostName, [int]$portText)
      $client.Dispose()
      return
    } catch { Start-Sleep -Milliseconds 100 }
  }
  throw "Timed out waiting for $Name at $Endpoint"
}

function Wait-ReadyLog {
  param([string] $Name, [string] $Pattern)
  $path = Join-Path $script:logDir "$Name.log"
  for ($i = 0; $i -lt 300; $i++) {
    if ((Test-Path $path) -and (Select-String -Path $path -SimpleMatch $Pattern -Quiet)) { return }
    Start-Sleep -Milliseconds 100
  }
  throw "Timed out waiting for $Name ready marker"
}

function Start-Redis {
  $name = "zlink-redis-node-supportchat-ps1-$([System.Guid]::NewGuid().ToString('N'))"
  $image = if ($env:ZLINK_REDIS_IMAGE) { $env:ZLINK_REDIS_IMAGE } else { "redis:7.2-alpine" }
  $id = Invoke-Docker -Arguments @("create", "--name", $name, "--tmpfs", "/data", "-p", "127.0.0.1::6379", $image)
  if (-not $id) { throw "Failed to create the dedicated Redis container" }
  Invoke-Docker -Arguments @("start", $id) | Out-Null
  if ((Invoke-Docker -Arguments @("inspect", "-f", "{{.State.Running}}", $id)) -ne 'true') { throw "Redis container is not running" }
  $ready = $false
  for ($i = 0; $i -lt 300; $i++) {
    try {
      if ((Invoke-Docker -Arguments @("exec", $id, "redis-cli", "ping") -TimeoutSeconds 5) -eq 'PONG') {
        $ready = $true
        break
      }
    } catch {}
    Start-Sleep -Milliseconds 100
  }
  if (-not $ready) { throw "Redis container did not return exact PONG before timeout" }
  $port = Invoke-Docker -Arguments @("inspect", "-f", "{{(index (index .NetworkSettings.Ports `"6379/tcp`") 0).HostPort}}", $id)
  if (-not $port) { throw "Redis host port was not assigned" }
  $env:SUPPORTCHAT_REDIS_ENDPOINT = "127.0.0.1:$port"
  Wait-Tcp -Name "redis" -Endpoint $env:SUPPORTCHAT_REDIS_ENDPOINT
  return $id
}

Push-Location $PSScriptRoot
try {
  npm run build | Out-Null
  $runDir = if ($env:SUPPORTCHAT_RUN_DIR) { $env:SUPPORTCHAT_RUN_DIR } else {
    Join-Path ([System.IO.Path]::GetTempPath()) ([System.Guid]::NewGuid().ToString("N"))
  }
  $script:logDir = Join-Path $runDir "logs"
  New-Item -ItemType Directory -Force -Path $script:logDir | Out-Null
  $env:SUPPORTCHAT_LOG_DIR = $script:logDir

  $portSet = [System.Collections.Generic.HashSet[int]]::new()
  while ($portSet.Count -lt 5) { [void]$portSet.Add((Get-FreeTcpPort)) }
  $ports = @($portSet)
  $env:SUPPORTCHAT_API_CHANNEL_ENDPOINT = "tcp://127.0.0.1:$($ports[0])"
  $env:SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT = "tcp://127.0.0.1:$($ports[1])"
  $env:SUPPORTCHAT_SUPPORT_SPOT_ENDPOINT = "tcp://127.0.0.1:$($ports[2])"
  $env:SUPPORTCHAT_SESSION_SPOT_ENDPOINT = "tcp://127.0.0.1:$($ports[3])"
  $env:SUPPORTCHAT_STREAM_ENDPOINT = "ws://127.0.0.1:$($ports[4])"
  $env:SUPPORTCHAT_REDIS_KEY_PREFIX = "supportchat:node:ps1:$([System.Guid]::NewGuid().ToString('N')):"
  $redisContainer = Start-Redis

  Start-Server -Name "support" -Path (Join-Path $PSScriptRoot "dist/Server/Support/main.js")
  Wait-ReadyLog -Name "support" -Pattern '"role":"support"'
  Wait-Tcp -Name "support-channel" -Endpoint $env:SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT
  Wait-Tcp -Name "support-spot" -Endpoint $env:SUPPORTCHAT_SUPPORT_SPOT_ENDPOINT
  Start-Server -Name "api" -Path (Join-Path $PSScriptRoot "dist/Server/Api/main.js")
  Wait-ReadyLog -Name "api" -Pattern '"role":"api"'
  Wait-Tcp -Name "api-channel" -Endpoint $env:SUPPORTCHAT_API_CHANNEL_ENDPOINT
  Start-Server -Name "session" -Path (Join-Path $PSScriptRoot "dist/Server/Session/main.js")
  Wait-ReadyLog -Name "session" -Pattern '"role":"session"'
  Wait-Tcp -Name "session-spot" -Endpoint $env:SUPPORTCHAT_SESSION_SPOT_ENDPOINT
  Wait-Tcp -Name "session-stream" -Endpoint $env:SUPPORTCHAT_STREAM_ENDPOINT

  node (Join-Path $PSScriptRoot "../../e2e/location-readiness.js") `
    --redis-endpoint $env:SUPPORTCHAT_REDIS_ENDPOINT `
    --key-prefix "$($env:SUPPORTCHAT_REDIS_KEY_PREFIX)location" `
    --peer client-server supportchat.api router $env:SUPPORTCHAT_API_CHANNEL_ENDPOINT `
    --peer client-server supportchat.support router $env:SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT `
    --peer spot-mesh supportchat-conversations spot $env:SUPPORTCHAT_SUPPORT_SPOT_ENDPOINT
  if ($LASTEXITCODE -ne 0) { throw "Location readiness failed" }

  $clientLog = Join-Path $script:logDir "client.log"
  node (Join-Path $PSScriptRoot "../../scripts/browser-e2e/run-sample.mjs") "SupportChat.Ts" *> $clientLog
  if ($LASTEXITCODE -ne 0) { throw "SupportChat client scenario failed" }
  foreach ($marker in @('supportchat=completed', 'supportchat-closed-typing-ignore=verified', 'PASS SupportChat.Ts', 'stream-inbound sample=SupportChat')) {
    if (-not (Select-String -Path $clientLog -SimpleMatch $marker -Quiet)) { throw "Missing client marker: $marker" }
  }
  Get-Content $clientLog
} finally {
  foreach ($process in $processes) {
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -ErrorAction SilentlyContinue }
  }
  if ($redisContainer) {
    try { Invoke-Docker -Arguments @("rm", "-f", "-v", $redisContainer) | Out-Null } catch {}
  }
  if ($runDir -and -not $env:SUPPORTCHAT_RUN_DIR -and $env:SUPPORTCHAT_KEEP_RUN_DIR -ne '1') {
    Remove-Item -Recurse -Force $runDir -ErrorAction SilentlyContinue
  } elseif ($runDir -and $env:SUPPORTCHAT_KEEP_RUN_DIR -eq '1') {
    Write-Output "runDir=$runDir"
  }
  Pop-Location
}
