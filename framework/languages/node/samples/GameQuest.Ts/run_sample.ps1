Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

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
}

Push-Location $PSScriptRoot
try {
  npm run build | Out-Null
  $runDir = if ($env:GAMEQUEST_RUN_DIR) { $env:GAMEQUEST_RUN_DIR } else { Join-Path ([System.IO.Path]::GetTempPath()) ([System.Guid]::NewGuid().ToString("N")) }
  $script:LogDir = Join-Path $runDir "logs"
  $workDir = Join-Path $runDir "work"
  New-Item -ItemType Directory -Force -Path $script:LogDir, $workDir | Out-Null
  $env:GAMEQUEST_WORK_DIR = $workDir
  $env:GAMEQUEST_LOG_DIR = $script:LogDir
  $env:GAMEQUEST_API_A_HTTP = "http://127.0.0.1:31301"
  $env:GAMEQUEST_API_B_HTTP = "http://127.0.0.1:31302"
  $env:GAMEQUEST_API_A_STREAM = "tcp://127.0.0.1:31303"
  $env:GAMEQUEST_API_B_STREAM = "tcp://127.0.0.1:31304"
  $env:GAMEQUEST_API_A_ROUTE = "tcp://127.0.0.1:31305"
  $env:GAMEQUEST_API_B_ROUTE = "tcp://127.0.0.1:31306"
  $env:GAMEQUEST_MISSION_A_ROUTE = "tcp://127.0.0.1:31307"
  $env:GAMEQUEST_MISSION_B_ROUTE = "tcp://127.0.0.1:31308"
  $env:GAMEQUEST_MISSION_A_ENDPOINT = $env:GAMEQUEST_MISSION_A_ROUTE
  $env:GAMEQUEST_MISSION_B_ENDPOINT = $env:GAMEQUEST_MISSION_B_ROUTE
  $env:GAMEQUEST_MISSION_A_SPOT_ROUTER = "tcp://127.0.0.1:31309"
  $env:GAMEQUEST_MISSION_B_SPOT_ROUTER = "tcp://127.0.0.1:31310"
  $env:GAMEQUEST_MISSION_A_SPOT = "tcp://127.0.0.1:31311"
  $env:GAMEQUEST_MISSION_B_SPOT = "tcp://127.0.0.1:31312"
  $env:GAMEQUEST_MISSION_A_HTTP = "http://127.0.0.1:31313"
  $env:GAMEQUEST_MISSION_B_HTTP = "http://127.0.0.1:31314"
  $env:GAMEQUEST_REDIS_ENDPOINT = "127.0.0.1:6379"
  $env:GAMEQUEST_REDIS_KEY_PREFIX = "gamequest:node:ps1:"

  $roles = @("mission-a", "mission-b", "api-a", "api-b") | ForEach-Object { Start-Role $_ }
  try {
    Wait-Topology
    node (Join-Path $PSScriptRoot "dist/Client/main.js")
  } finally {
    $roles | ForEach-Object { Stop-Process -Id $_.Id -ErrorAction SilentlyContinue }
  }
} finally {
  Pop-Location
}
