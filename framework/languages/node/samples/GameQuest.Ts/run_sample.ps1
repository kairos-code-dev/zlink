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

function Start-Role {
  param([string] $Name)
  Start-Process -FilePath "node" -ArgumentList @((Join-Path $PSScriptRoot "dist/Server/main.js"), "--role", $Name) -PassThru -RedirectStandardOutput (Join-Path $script:LogDir "$Name.log") -RedirectStandardError (Join-Path $script:LogDir "$Name.err.log")
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

function Wait-Topology {
  Wait-Http -Url $env:GAMEQUEST_API_A_HTTP
  Wait-Http -Url $env:GAMEQUEST_API_B_HTTP
  Wait-Http -Url $env:GAMEQUEST_MISSION_A_HTTP
  Wait-Http -Url $env:GAMEQUEST_MISSION_B_HTTP
}

function Get-FreeTcpPorts {
  param([int] $Count)
  $listeners = [System.Collections.Generic.List[System.Net.Sockets.TcpListener]]::new()
  try {
    for ($i = 0; $i -lt $Count; $i++) {
      $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
      $listener.Start()
      $listeners.Add($listener)
    }
    return @($listeners | ForEach-Object { ([System.Net.IPEndPoint]$_.LocalEndpoint).Port })
  } finally {
    $listeners | ForEach-Object { $_.Stop() }
  }
}

function Wait-Redis {
  param([string] $Container)
  for ($i = 0; $i -lt 300; $i++) {
    try {
      if ((Invoke-Docker -Arguments @("exec", $Container, "redis-cli", "PING") -TimeoutSeconds 5) -eq "PONG") { return }
    } catch {}
    Start-Sleep -Milliseconds 100
  }
  throw "Timed out waiting for Redis container $Container"
}

Push-Location $PSScriptRoot
$redisContainer = $null
$redisName = $null
try {
  npm run build | Out-Null
  $runDir = if ($env:GAMEQUEST_RUN_DIR) { $env:GAMEQUEST_RUN_DIR } else { Join-Path ([System.IO.Path]::GetTempPath()) ([System.Guid]::NewGuid().ToString("N")) }
  $script:LogDir = Join-Path $runDir "logs"
  $workDir = Join-Path $runDir "work"
  New-Item -ItemType Directory -Force -Path $script:LogDir, $workDir | Out-Null
  $env:GAMEQUEST_WORK_DIR = $workDir
  $env:GAMEQUEST_LOG_DIR = $script:LogDir
  $ports = Get-FreeTcpPorts -Count 14
  $env:GAMEQUEST_API_A_HTTP = "http://127.0.0.1:$($ports[0])"
  $env:GAMEQUEST_API_B_HTTP = "http://127.0.0.1:$($ports[1])"
  $env:GAMEQUEST_API_A_STREAM = "ws://127.0.0.1:$($ports[2])"
  $env:GAMEQUEST_API_B_STREAM = "ws://127.0.0.1:$($ports[3])"
  $env:GAMEQUEST_API_A_ACTOR_SPOT = "tcp://127.0.0.1:$($ports[4])"
  $env:GAMEQUEST_API_B_ACTOR_SPOT = "tcp://127.0.0.1:$($ports[5])"
  $env:GAMEQUEST_MISSION_A_ROUTE = "tcp://127.0.0.1:$($ports[6])"
  $env:GAMEQUEST_MISSION_B_ROUTE = "tcp://127.0.0.1:$($ports[7])"
  $env:GAMEQUEST_MISSION_A_SPOT_ROUTER = "tcp://127.0.0.1:$($ports[8])"
  $env:GAMEQUEST_MISSION_B_SPOT_ROUTER = "tcp://127.0.0.1:$($ports[9])"
  $env:GAMEQUEST_MISSION_A_SPOT = "tcp://127.0.0.1:$($ports[10])"
  $env:GAMEQUEST_MISSION_B_SPOT = "tcp://127.0.0.1:$($ports[11])"
  $env:GAMEQUEST_MISSION_A_HTTP = "http://127.0.0.1:$($ports[12])"
  $env:GAMEQUEST_MISSION_B_HTTP = "http://127.0.0.1:$($ports[13])"
  $runId = [System.Guid]::NewGuid().ToString('N')
  $redisName = "zlink-redis-node-gamequest-$runId"
  $redisContainer = Invoke-Docker -Arguments @("create", "--name", $redisName, "-p", "127.0.0.1::6379", "--tmpfs", "/data", "redis:7.2-alpine")
  if ([string]::IsNullOrWhiteSpace($redisContainer)) {
    throw "Unable to create the dedicated GameQuest Redis container."
  }
  Invoke-Docker -Arguments @("start", $redisContainer) | Out-Null
  if ((Invoke-Docker -Arguments @("inspect", "-f", "{{.State.Running}}", $redisContainer)) -ne "true") {
    throw "Dedicated GameQuest Redis container did not enter the running state."
  }
  $redisPortLine = Invoke-Docker -Arguments @("port", $redisContainer, "6379/tcp")
  if ($redisPortLine -notmatch ':(\d+)$') {
    throw "Unable to inspect the dedicated GameQuest Redis port."
  }
  $redisPort = [int]$Matches[1]
  Wait-Redis -Container $redisContainer
  $env:GAMEQUEST_REDIS_ENDPOINT = "127.0.0.1:$redisPort"
  $env:GAMEQUEST_REDIS_KEY_PREFIX = "gamequest:node:ps1:${runId}:"

  $roles = @("mission-a", "mission-b", "api-a", "api-b") | ForEach-Object { Start-Role $_ }
  try {
    Wait-Topology
    & node (Join-Path $PSScriptRoot "../../scripts/browser-e2e/run-sample.mjs") "GameQuest.Ts"
    if ($LASTEXITCODE -ne 0) { throw "GameQuest client scenario failed" }
  } finally {
    $roles | ForEach-Object { Stop-Process -Id $_.Id -ErrorAction SilentlyContinue }
  }
} finally {
  if ($null -ne $redisContainer) {
    try { Invoke-Docker -Arguments @("rm", "-f", "-v", $redisContainer) | Out-Null } catch {}
  } elseif ($null -ne $redisName) {
    try { Invoke-Docker -Arguments @("rm", "-f", "-v", $redisName) | Out-Null } catch {}
  }
  Pop-Location
}
